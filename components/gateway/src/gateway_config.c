#include "gateway_config.h"

#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "gateway_topic.h"
#include "platform_nvs.h"

#define NS "gw_config"

static gateway_config_t s_config;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void gateway_config_build_ap_identity(const char *gateway_id,
                                      char *ssid, size_t ssid_capacity,
                                      char *password, size_t password_capacity)
{
    const char *id = gateway_id && gateway_id[0] ? gateway_id : "GW-01";
    if (password && password_capacity)
        snprintf(password, password_capacity, "AUBOT-%s", id);
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
    snprintf(c->warehouse_id, sizeof(c->warehouse_id), "kho-01");
    snprintf(c->warehouse_name, sizeof(c->warehouse_name), "Kho 01");
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

esp_err_t gateway_config_init(void)
{
    defaults(&s_config);
    platform_nvs_handle_t h = {0};
    esp_err_t err = platform_nvs_open(&h, NS, true);
    if (err == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    get_str(&h, "gw_id", s_config.gateway_id, sizeof(s_config.gateway_id));
    get_str(&h, "company_id", s_config.company_id, sizeof(s_config.company_id));
    get_str(&h, "site_id", s_config.site_id, sizeof(s_config.site_id));
    get_str(&h, "warehouse_id", s_config.warehouse_id, sizeof(s_config.warehouse_id));
    get_str(&h, "warehouse_name", s_config.warehouse_name, sizeof(s_config.warehouse_name));
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
    gateway_config_build_ap_identity(s_config.gateway_id, NULL, 0,
                                     s_config.ap_password, sizeof(s_config.ap_password));
    return ESP_OK;
}

void gateway_config_get(gateway_config_t *config)
{
    if (!config) return;
    taskENTER_CRITICAL(&s_mux); *config = s_config; taskEXIT_CRITICAL(&s_mux);
}

esp_err_t gateway_config_save(const gateway_config_t *c)
{
    if (!c || c->gateway_id[0] == 0 || c->wifi_profile_count > GATEWAY_WIFI_PROFILE_MAX ||
        !gateway_identity_valid(c) ||
        c->mqtt_port == 0 || c->publish_interval_ms < 250 || c->publish_interval_ms > 60000 ||
        c->sntp_primary[0] == 0 || c->timezone[0] == 0) return ESP_ERR_INVALID_ARG;
    gateway_config_t normalized = *c;
    gateway_config_build_ap_identity(normalized.gateway_id, NULL, 0,
                                     normalized.ap_password, sizeof(normalized.ap_password));
    c = &normalized;
    platform_nvs_handle_t h = {0};
    esp_err_t e = platform_nvs_open(&h, NS, false);
#define SETS(k,v) do { if (e == ESP_OK) e = platform_nvs_set_string(&h,(k),(v)); } while (0)
#define SET8(k,v) do { if (e == ESP_OK) e = platform_nvs_set_u8(&h,(k),(v)); } while (0)
#define SET16(k,v) do { if (e == ESP_OK) e = platform_nvs_set_u16(&h,(k),(v)); } while (0)
    if (e != ESP_OK) return e;
    SETS("gw_id", c->gateway_id);
    SETS("company_id", c->company_id);
    SETS("site_id", c->site_id);
    SETS("warehouse_id", c->warehouse_id);
    SETS("warehouse_name", c->warehouse_name);
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
    if (e == ESP_OK) { taskENTER_CRITICAL(&s_mux); s_config = *c; taskEXIT_CRITICAL(&s_mux); }
    return e;
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
