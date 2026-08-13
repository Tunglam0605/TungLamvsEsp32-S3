#include "gateway_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gateway_topic.h"
#include "platform_nvs.h"

#define NS "gw_config"

static const char *TAG = "GW_CONFIG";

static gateway_config_t s_config;
static gateway_config_t s_pending_mqtt_identity;
static bool s_pending_mqtt_identity_valid;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_store_lock;
/* gateway_config_t contains five Wi-Fi profiles and is about 1.2 KiB. Keep
 * migration/save scratch storage in BSS: putting several copies on ESP-IDF's
 * 3.5 KiB main/HTTP task stacks causes an immediate stack overflow before the
 * first boot migration can commit. The save mutex serializes this workspace. */
static gateway_config_t s_init_stored_identity;
static struct {
    gateway_config_t normalized;
    gateway_config_t previous;
    gateway_config_t pending;
} s_save_work;

#define PENDING_KEY       "pending_mq"
#define OLD_COMPANY_KEY   "old_company"
#define OLD_SITE_KEY      "old_site"
#define OLD_WAREHOUSE_KEY "old_warehouse"
#define OLD_WH_NAME_KEY   "old_wh_name"

bool gateway_config_gateway_id_valid(const char *gateway_id)
{
    if (gateway_id == NULL) return false;
    const size_t length = strnlen(gateway_id, sizeof(((gateway_config_t *)0)->gateway_id));
    if (length == 0U || length >= sizeof(((gateway_config_t *)0)->gateway_id)) return false;
    const unsigned char first = (unsigned char)gateway_id[0];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') ||
          (first >= '0' && first <= '9'))) return false;
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char c = (unsigned char)gateway_id[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            continue;
        }
        return false;
    }
    return true;
}

bool gateway_config_derive_warehouse_identity(gateway_config_t *config)
{
    if (config == NULL || !gateway_config_gateway_id_valid(config->gateway_id)) {
        return false;
    }
    size_t i = 0U;
    for (; config->gateway_id[i] != '\0' && i + 1U < sizeof(config->warehouse_id); ++i) {
        const unsigned char c = (unsigned char)config->gateway_id[i];
        config->warehouse_id[i] = (char)tolower(c);
    }
    config->warehouse_id[i] = '\0';
    strlcpy(config->warehouse_name, config->gateway_id,
            sizeof(config->warehouse_name));
    return true;
}

void gateway_config_build_ap_identity(const char *gateway_id,
                                      char *ssid, size_t ssid_capacity,
                                      char *password, size_t password_capacity)
{
    const char *id = gateway_id && gateway_id[0] ? gateway_id : "GW-01";
    if (password && password_capacity) {
        snprintf(password, password_capacity, "AUBOT-%s", id);
        /* ESP-IDF turns an AP with a password shorter than eight characters
         * into an open network. Keep the documented AUBOT-{gateway_id} form
         * and only pad the one-character edge case. */
        size_t length = strnlen(password, password_capacity);
        while (length < 8U && length + 1U < password_capacity) {
            password[length++] = '0';
        }
        if (length < password_capacity) password[length] = '\0';
    }
    if (!ssid || !ssid_capacity) return;
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK)
        snprintf(ssid, ssid_capacity, "AUBOT-%s-%02X%02X%02X",
                 id, mac[3], mac[4], mac[5]);
    else
        snprintf(ssid, ssid_capacity, "AUBOT-%s", id);
}

static void defaults(gateway_config_t *c)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->gateway_id, sizeof(c->gateway_id), "GW-01");
    snprintf(c->company_id, sizeof(c->company_id), "aubot");
    snprintf(c->site_id, sizeof(c->site_id), "ha-noi");
    (void)gateway_config_derive_warehouse_identity(c);
    gateway_config_build_ap_identity(c->gateway_id, NULL, 0,
                                     c->ap_password, sizeof(c->ap_password));
    c->wifi_dhcp = true;
    snprintf(c->wifi_ip, sizeof(c->wifi_ip), "192.168.1.204");
    snprintf(c->wifi_netmask, sizeof(c->wifi_netmask), "255.255.255.0");
    snprintf(c->wifi_gateway, sizeof(c->wifi_gateway), "192.168.1.1");
    snprintf(c->wifi_dns, sizeof(c->wifi_dns), "8.8.8.8");
    c->eth_router_mode = false;
    c->eth_dhcp = true;
    snprintf(c->eth_ip, sizeof(c->eth_ip), "192.168.1.205");
    snprintf(c->eth_netmask, sizeof(c->eth_netmask), "255.255.255.0");
    snprintf(c->eth_gateway, sizeof(c->eth_gateway), "192.168.1.1");
    snprintf(c->eth_dns, sizeof(c->eth_dns), "8.8.8.8");
    c->mqtt_port = 1883;
    c->mqtt_transport = GATEWAY_MQTT_TCP;
    c->publish_interval_ms = 1000;
    snprintf(c->sntp_primary, sizeof(c->sntp_primary), "pool.ntp.org");
    snprintf(c->sntp_fallback, sizeof(c->sntp_fallback), "time.google.com");
    snprintf(c->timezone, sizeof(c->timezone), "ICT-7");
    gateway_config_add_wifi(c, "Robotics AUBOT 1", "123456789");
}

static void get_str(platform_nvs_handle_t *h, const char *key, char *dst, size_t n)
{
    bool found = false;
    (void)platform_nvs_get_string(h, key, dst, n, &found);
}

static bool get_required_str(platform_nvs_handle_t *handle, const char *key,
                             char *destination, size_t capacity)
{
    bool found = false;
    return platform_nvs_get_string(handle, key, destination, capacity, &found) == ESP_OK &&
           found;
}

static void build_recovery_gateway_id(char *destination, size_t capacity)
{
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(destination, capacity, "GW-%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
    } else {
        strlcpy(destination, "GW-01", capacity);
    }
}

static bool mqtt_namespace_changed(const gateway_config_t *before,
                                   const gateway_config_t *after)
{
    return strcmp(before->company_id, after->company_id) != 0 ||
           strcmp(before->site_id, after->site_id) != 0 ||
           strcmp(before->warehouse_id, after->warehouse_id) != 0;
}

esp_err_t gateway_config_init(void)
{
    if (s_store_lock == NULL) s_store_lock = xSemaphoreCreateMutex();
    if (s_store_lock == NULL) return ESP_ERR_NO_MEM;
    defaults(&s_config);
    memset(&s_pending_mqtt_identity, 0, sizeof(s_pending_mqtt_identity));
    s_pending_mqtt_identity_valid = false;
    platform_nvs_handle_t h = {0};
    esp_err_t err = platform_nvs_open(&h, NS, true);
    if (err == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    bool identity_repair_required = false;
    char stored_gateway_id[sizeof(s_config.gateway_id)] = {0};
    bool gateway_id_found = false;
    const esp_err_t gateway_id_error = platform_nvs_get_string(
        &h, "gw_id", stored_gateway_id, sizeof(stored_gateway_id),
        &gateway_id_found);
    if (gateway_id_error == ESP_OK && gateway_id_found &&
        gateway_config_gateway_id_valid(stored_gateway_id)) {
        strlcpy(s_config.gateway_id, stored_gateway_id,
                sizeof(s_config.gateway_id));
    } else if (gateway_id_found || gateway_id_error != ESP_OK) {
        build_recovery_gateway_id(s_config.gateway_id,
                                  sizeof(s_config.gateway_id));
        identity_repair_required = true;
        ESP_LOGW(TAG, "Invalid stored Gateway ID; recovered as %s",
                 s_config.gateway_id);
    }
    get_str(&h, "company_id", s_config.company_id, sizeof(s_config.company_id));
    get_str(&h, "site_id", s_config.site_id, sizeof(s_config.site_id));
    if (!gateway_topic_segment_valid(s_config.company_id)) {
        strlcpy(s_config.company_id, "aubot", sizeof(s_config.company_id));
        identity_repair_required = true;
        ESP_LOGW(TAG, "Invalid stored company ID; restored safe default");
    }
    if (!gateway_topic_segment_valid(s_config.site_id)) {
        strlcpy(s_config.site_id, "ha-noi", sizeof(s_config.site_id));
        identity_repair_required = true;
        ESP_LOGW(TAG, "Invalid stored site ID; restored safe default");
    }
    s_init_stored_identity = s_config;
    get_str(&h, "warehouse_id", s_init_stored_identity.warehouse_id,
            sizeof(s_init_stored_identity.warehouse_id));
    get_str(&h, "warehouse_name", s_init_stored_identity.warehouse_name,
            sizeof(s_init_stored_identity.warehouse_name));
    get_str(&h, "wifi_ip", s_config.wifi_ip, sizeof(s_config.wifi_ip));
    get_str(&h, "wifi_mask", s_config.wifi_netmask, sizeof(s_config.wifi_netmask));
    get_str(&h, "wifi_gw", s_config.wifi_gateway, sizeof(s_config.wifi_gateway));
    get_str(&h, "wifi_dns", s_config.wifi_dns, sizeof(s_config.wifi_dns));
    get_str(&h, "eth_ip", s_config.eth_ip, sizeof(s_config.eth_ip));
    get_str(&h, "eth_mask", s_config.eth_netmask, sizeof(s_config.eth_netmask));
    get_str(&h, "eth_gw", s_config.eth_gateway, sizeof(s_config.eth_gateway));
    get_str(&h, "eth_dns", s_config.eth_dns, sizeof(s_config.eth_dns));
    get_str(&h, "mq_host", s_config.mqtt_broker, sizeof(s_config.mqtt_broker));
    get_str(&h, "mq_user", s_config.mqtt_user, sizeof(s_config.mqtt_user));
    get_str(&h, "mq_pass", s_config.mqtt_password, sizeof(s_config.mqtt_password));
    get_str(&h, "ntp_main", s_config.sntp_primary, sizeof(s_config.sntp_primary));
    get_str(&h, "ntp_alt", s_config.sntp_fallback, sizeof(s_config.sntp_fallback));
    get_str(&h, "timezone", s_config.timezone, sizeof(s_config.timezone));
    bool found = false;
    uint8_t v8 = 0;
    uint16_t v16 = 0;
    if (platform_nvs_get_u8(&h, "wifi_dhcp", &v8, &found) == ESP_OK && found) s_config.wifi_dhcp = v8 != 0;
    if (platform_nvs_get_u8(&h, "eth_mode", &v8, &found) == ESP_OK && found) s_config.eth_router_mode = v8 != 0;
    if (platform_nvs_get_u8(&h, "eth_dhcp", &v8, &found) == ESP_OK && found) s_config.eth_dhcp = v8 != 0;
    if (platform_nvs_get_u8(&h, "mq_tls", &v8, &found) == ESP_OK && found) s_config.mqtt_transport = v8 ? GATEWAY_MQTT_TLS : GATEWAY_MQTT_TCP;
    if (platform_nvs_get_u16(&h, "mq_port", &v16, &found) == ESP_OK && found) s_config.mqtt_port = v16;
    if (platform_nvs_get_u16(&h, "pub_ms", &v16, &found) == ESP_OK && found) s_config.publish_interval_ms = v16;
    uint8_t pending = 0U;
    if (platform_nvs_get_u8(&h, PENDING_KEY, &pending, &found) == ESP_OK &&
        found && pending != 0U) {
        memset(&s_pending_mqtt_identity, 0, sizeof(s_pending_mqtt_identity));
        const bool complete =
            get_required_str(&h, OLD_COMPANY_KEY,
                             s_pending_mqtt_identity.company_id,
                             sizeof(s_pending_mqtt_identity.company_id)) &&
            get_required_str(&h, OLD_SITE_KEY,
                             s_pending_mqtt_identity.site_id,
                             sizeof(s_pending_mqtt_identity.site_id)) &&
            get_required_str(&h, OLD_WAREHOUSE_KEY,
                             s_pending_mqtt_identity.warehouse_id,
                             sizeof(s_pending_mqtt_identity.warehouse_id)) &&
            get_required_str(&h, OLD_WH_NAME_KEY,
                             s_pending_mqtt_identity.warehouse_name,
                             sizeof(s_pending_mqtt_identity.warehouse_name));
        if (complete && gateway_identity_valid(&s_pending_mqtt_identity)) {
            s_pending_mqtt_identity_valid = true;
        } else {
            /* Never synthesize a hybrid old/new topic from partial NVS data.
             * A later config transaction clears this corrupt marker without
             * publishing a retained delete to an uncertain destination. */
            identity_repair_required = true;
            ESP_LOGW(TAG, "Ignoring incomplete retained-topic cleanup marker");
        }
    }
    uint8_t count = 0;
    if (platform_nvs_get_u8(&h, "wifi_count", &count, &found) == ESP_OK && found) {
        s_config.wifi_profile_count = count > GATEWAY_WIFI_PROFILE_MAX ? GATEWAY_WIFI_PROFILE_MAX : count;
        for (uint8_t i = 0; i < s_config.wifi_profile_count; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "ssid%u", i); get_str(&h, key, s_config.wifi_profiles[i].ssid, sizeof(s_config.wifi_profiles[i].ssid));
            snprintf(key, sizeof(key), "pass%u", i); get_str(&h, key, s_config.wifi_profiles[i].password, sizeof(s_config.wifi_profiles[i].password));
        }
    }
    platform_nvs_close(&h);
    if (!gateway_config_derive_warehouse_identity(&s_config)) {
        return ESP_ERR_INVALID_ARG;
    }
    gateway_config_build_ap_identity(s_config.gateway_id, NULL, 0,
                                     s_config.ap_password, sizeof(s_config.ap_password));
    if (!s_pending_mqtt_identity_valid &&
        gateway_identity_valid(&s_init_stored_identity) &&
        mqtt_namespace_changed(&s_init_stored_identity, &s_config)) {
        /* Persist the old namespace and the derived replacement in one normal
         * config transaction.  A reset before broker PUBACK can therefore
         * resume retained-topic cleanup. */
        s_pending_mqtt_identity = s_init_stored_identity;
        s_pending_mqtt_identity_valid = true;
        return gateway_config_save(&s_config);
    }
    /* Persist a current-schema identity even when an older pending namespace
     * already exists. gateway_config_save() preserves that pending record in
     * the same transaction until MQTT confirms retained cleanup. */
    if (identity_repair_required ||
        strcmp(s_init_stored_identity.warehouse_id, s_config.warehouse_id) != 0 ||
        strcmp(s_init_stored_identity.warehouse_name, s_config.warehouse_name) != 0) {
        return gateway_config_save(&s_config);
    }
    return ESP_OK;
}

void gateway_config_get(gateway_config_t *config)
{
    if (!config) return;
    taskENTER_CRITICAL(&s_mux); *config = s_config; taskEXIT_CRITICAL(&s_mux);
}

esp_err_t gateway_config_save(const gateway_config_t *c)
{
    if (!c || !gateway_config_gateway_id_valid(c->gateway_id) ||
        c->wifi_profile_count > GATEWAY_WIFI_PROFILE_MAX ||
        c->mqtt_port == 0 || c->publish_interval_ms < 250 || c->publish_interval_ms > 60000 ||
        c->sntp_primary[0] == 0 || c->timezone[0] == 0) return ESP_ERR_INVALID_ARG;
    if (s_store_lock == NULL || xSemaphoreTake(s_store_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    s_save_work.normalized = *c;
    if (!gateway_config_derive_warehouse_identity(&s_save_work.normalized) ||
        !gateway_identity_valid(&s_save_work.normalized)) {
        xSemaphoreGive(s_store_lock);
        return ESP_ERR_INVALID_ARG;
    }
    gateway_config_build_ap_identity(
        s_save_work.normalized.gateway_id, NULL, 0,
        s_save_work.normalized.ap_password,
        sizeof(s_save_work.normalized.ap_password));
    c = &s_save_work.normalized;
    memset(&s_save_work.pending, 0, sizeof(s_save_work.pending));
    bool persist_pending = false;
    taskENTER_CRITICAL(&s_mux);
    s_save_work.previous = s_config;
    if (s_pending_mqtt_identity_valid) {
        s_save_work.pending = s_pending_mqtt_identity;
        persist_pending = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    const bool queue_previous = !persist_pending &&
        gateway_identity_valid(&s_save_work.previous) &&
        mqtt_namespace_changed(&s_save_work.previous, c);
    if (queue_previous) {
        s_save_work.pending = s_save_work.previous;
        persist_pending = true;
    }
    const bool pending_matches_previous = persist_pending &&
        !mqtt_namespace_changed(&s_save_work.pending, &s_save_work.previous);
    if (persist_pending && !pending_matches_previous &&
        mqtt_namespace_changed(&s_save_work.previous, c)) {
        /* One durable cleanup is already in flight. Refuse to create another
         * old retained namespace that cannot fit in the single pending slot. */
        xSemaphoreGive(s_store_lock);
        return ESP_ERR_INVALID_STATE;
    }
    platform_nvs_handle_t h = {0};
    esp_err_t e = platform_nvs_open(&h, NS, false);
#define SETS(k,v) do { if (e == ESP_OK) e = platform_nvs_set_string(&h,(k),(v)); } while (0)
#define SET8(k,v) do { if (e == ESP_OK) e = platform_nvs_set_u8(&h,(k),(v)); } while (0)
#define SET16(k,v) do { if (e == ESP_OK) e = platform_nvs_set_u16(&h,(k),(v)); } while (0)
    if (e != ESP_OK) {
        xSemaphoreGive(s_store_lock);
        return e;
    }
    SETS("gw_id", c->gateway_id);
    SETS("company_id", c->company_id);
    SETS("site_id", c->site_id);
    SETS("warehouse_id", c->warehouse_id);
    SETS("warehouse_name", c->warehouse_name);
    if (persist_pending) {
        SETS(OLD_COMPANY_KEY, s_save_work.pending.company_id);
        SETS(OLD_SITE_KEY, s_save_work.pending.site_id);
        SETS(OLD_WAREHOUSE_KEY, s_save_work.pending.warehouse_id);
        SETS(OLD_WH_NAME_KEY, s_save_work.pending.warehouse_name);
        SET8(PENDING_KEY, 1U);
    } else {
        SET8(PENDING_KEY, 0U);
    }
    SET8("wifi_dhcp", c->wifi_dhcp); SETS("wifi_ip", c->wifi_ip); SETS("wifi_mask", c->wifi_netmask);
    SETS("wifi_gw", c->wifi_gateway); SETS("wifi_dns", c->wifi_dns);
    SET8("eth_mode", c->eth_router_mode); SET8("eth_dhcp", c->eth_dhcp); SETS("eth_ip", c->eth_ip); SETS("eth_mask", c->eth_netmask);
    SETS("eth_gw", c->eth_gateway); SETS("eth_dns", c->eth_dns);
    SETS("mq_host", c->mqtt_broker); SET16("mq_port", c->mqtt_port); SET8("mq_tls", c->mqtt_transport == GATEWAY_MQTT_TLS);
    SETS("mq_user", c->mqtt_user); SETS("mq_pass", c->mqtt_password); SET16("pub_ms", c->publish_interval_ms);
    SETS("ntp_main", c->sntp_primary); SETS("ntp_alt", c->sntp_fallback); SETS("timezone", c->timezone);
    SET8("wifi_count", c->wifi_profile_count);
    for (uint8_t i = 0; i < GATEWAY_WIFI_PROFILE_MAX; ++i) {
        char key[8]; const char *ssid = i < c->wifi_profile_count ? c->wifi_profiles[i].ssid : "";
        const char *pass = i < c->wifi_profile_count ? c->wifi_profiles[i].password : "";
        snprintf(key, sizeof(key), "ssid%u", i); SETS(key, ssid);
        snprintf(key, sizeof(key), "pass%u", i); SETS(key, pass);
    }
    if (e == ESP_OK) e = platform_nvs_commit(&h);
    platform_nvs_close(&h);
    if (e == ESP_OK) {
        taskENTER_CRITICAL(&s_mux);
        s_config = *c;
        if (persist_pending) {
            s_pending_mqtt_identity = s_save_work.pending;
            s_pending_mqtt_identity_valid = true;
        } else {
            memset(&s_pending_mqtt_identity, 0,
                   sizeof(s_pending_mqtt_identity));
            s_pending_mqtt_identity_valid = false;
        }
        taskEXIT_CRITICAL(&s_mux);
    }
    xSemaphoreGive(s_store_lock);
    return e;
}

bool gateway_config_get_pending_mqtt_identity(gateway_config_t *config)
{
    if (config == NULL) return false;
    bool valid;
    taskENTER_CRITICAL(&s_mux);
    valid = s_pending_mqtt_identity_valid;
    if (valid) *config = s_pending_mqtt_identity;
    taskEXIT_CRITICAL(&s_mux);
    return valid;
}

esp_err_t gateway_config_clear_pending_mqtt_identity(void)
{
    if (s_store_lock == NULL || xSemaphoreTake(s_store_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    platform_nvs_handle_t h = {0};
    esp_err_t error = platform_nvs_open(&h, NS, false);
    if (error != ESP_OK) {
        xSemaphoreGive(s_store_lock);
        return error;
    }
    error = platform_nvs_set_u8(&h, PENDING_KEY, 0U);
    if (error == ESP_OK) error = platform_nvs_commit(&h);
    platform_nvs_close(&h);
    if (error == ESP_OK) {
        taskENTER_CRITICAL(&s_mux);
        memset(&s_pending_mqtt_identity, 0, sizeof(s_pending_mqtt_identity));
        s_pending_mqtt_identity_valid = false;
        taskEXIT_CRITICAL(&s_mux);
    }
    xSemaphoreGive(s_store_lock);
    return error;
}

bool gateway_config_add_wifi(gateway_config_t *c, const char *ssid, const char *password)
{
    if (!c || !ssid || !ssid[0] || strnlen(ssid, 33) >= 33 || !password || strnlen(password, 64) >= 64) return false;
    gateway_wifi_profile_t selected = {0};
    snprintf(selected.ssid, sizeof(selected.ssid), "%s", ssid);
    snprintf(selected.password, sizeof(selected.password), "%s", password);
    uint8_t count = c->wifi_profile_count;
    if (count > GATEWAY_WIFI_PROFILE_MAX) count = GATEWAY_WIFI_PROFILE_MAX;
    uint8_t index = count;
    for (uint8_t i = 0; i < count; ++i) {
        if (!strcmp(c->wifi_profiles[i].ssid, ssid)) {
            index = i;
            break;
        }
    }
    if (index == count) {
        if (count < GATEWAY_WIFI_PROFILE_MAX) ++count;
        index = count - 1U;
    }
    for (uint8_t i = index; i > 0U; --i) c->wifi_profiles[i] = c->wifi_profiles[i - 1U];
    c->wifi_profiles[0] = selected;
    c->wifi_profile_count = count;
    return true;
}

bool gateway_config_remove_wifi(gateway_config_t *c, const char *ssid)
{
    if (!c || !ssid) return false;
    for (uint8_t i = 0; i < c->wifi_profile_count; ++i) if (!strcmp(c->wifi_profiles[i].ssid, ssid)) {
        memmove(&c->wifi_profiles[i], &c->wifi_profiles[i + 1], sizeof(c->wifi_profiles[0]) * (c->wifi_profile_count - i - 1));
        memset(&c->wifi_profiles[--c->wifi_profile_count], 0, sizeof(c->wifi_profiles[0])); return true;
    }
    return false;
}
