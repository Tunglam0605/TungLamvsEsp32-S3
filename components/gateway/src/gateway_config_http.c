#include "gateway_config_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_eth.h"
#include "gateway_auth.h"
#include "gateway_config.h"
#include "gateway_config_page.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "gateway_topic.h"
#include "gateway_web_theme.h"
#include "platform_wifi.h"
#include "platform_time.h"
#include <time.h>

#define VALID_UNIX_TIME 1704067200LL

typedef enum {
    APPLY_NETWORK = 1U << 0,
    APPLY_MQTT = 1U << 1,
    APPLY_TIME = 1U << 2,
} config_apply_flag_t;

static int nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void decode(char *text)
{
    char *output = text;
    for (; *text; ++text) {
        if (*text == '+') *output++ = ' ';
        else if (*text == '%' && nibble(text[1]) >= 0 && nibble(text[2]) >= 0) {
            *output++ = (char)((nibble(text[1]) << 4) | nibble(text[2]));
            text += 2;
        } else *output++ = *text;
    }
    *output = '\0';
}

static bool field(const char *body, const char *key, char *output, size_t capacity)
{
    if (httpd_query_key_value(body, key, output, capacity) != ESP_OK) return false;
    decode(output);
    return true;
}

static size_t json_escape(char *output, size_t capacity, const char *input)
{
    size_t used = 0U;
    for (; *input && used + 1U < capacity; ++input) {
        const char *escape = NULL;
        if (*input == '"') escape = "\\\"";
        else if (*input == '\\') escape = "\\\\";
        else if (*input == '\n') escape = "\\n";
        else if (*input == '\r') escape = "\\r";
        else if (*input == '\t') escape = "\\t";
        if (escape) {
            const size_t length = strlen(escape);
            if (used + length >= capacity) break;
            memcpy(output + used, escape, length);
            used += length;
        } else if ((unsigned char)*input >= 0x20U) {
            output[used++] = *input;
        }
    }
    output[used] = '\0';
    return used;
}

static esp_err_t read_body(httpd_req_t *request, char *body, size_t capacity)
{
    if (!request->content_len || request->content_len >= capacity) return ESP_ERR_INVALID_SIZE;
    size_t total = 0U;
    while (total < request->content_len) {
        const int received = httpd_req_recv(request, body + total, request->content_len - total);
        if (received <= 0) return ESP_FAIL;
        total += (size_t)received;
    }
    body[total] = '\0';
    return ESP_OK;
}

static esp_err_t page(httpd_req_t *request)
{
    if (!gateway_auth_require_page(request, GW_PERMISSION_NETWORK_CONFIG, NULL)) return ESP_OK;
    return gateway_web_send_html(request, GATEWAY_CONFIG_PAGE);
}

static esp_err_t legacy_login(httpd_req_t *request)
{
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/login");
    return httpd_resp_sendstr(request, "");
}

static esp_err_t config_get(httpd_req_t *request)
{
    gateway_auth_session_t session;
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, &session)) return ESP_OK;
    const bool it = gateway_auth_role_has_permission(session.role, GW_PERMISSION_MQTT_CONFIG);
    const bool ethernet = gateway_auth_role_has_permission(session.role,
                                                            GW_PERMISSION_ETHERNET_CONFIG);
    gateway_config_t config;
    gateway_config_get(&config);
    gateway_topic_set_t topics = {0};
    (void)gateway_topic_build_set(&config, &topics);
    char gateway_id[40], company_id[72], site_id[72];
    char warehouse_id[72], warehouse_name[160];
    char mqtt_broker[192], mqtt_user[96];
    char ntp_primary[128], ntp_fallback[128], timezone[128];
    json_escape(gateway_id, sizeof(gateway_id), config.gateway_id);
    json_escape(company_id, sizeof(company_id), config.company_id);
    json_escape(site_id, sizeof(site_id), config.site_id);
    json_escape(warehouse_id, sizeof(warehouse_id), config.warehouse_id);
    json_escape(warehouse_name, sizeof(warehouse_name), config.warehouse_name);
    json_escape(mqtt_broker, sizeof(mqtt_broker), config.mqtt_broker);
    json_escape(mqtt_user, sizeof(mqtt_user), config.mqtt_user);
    json_escape(ntp_primary, sizeof(ntp_primary), config.sntp_primary);
    json_escape(ntp_fallback, sizeof(ntp_fallback), config.sntp_fallback);
    json_escape(timezone, sizeof(timezone), config.timezone);
    platform_wifi_sta_status_t wifi = {0};
    (void)platform_wifi_get_sta_status(&wifi);
    char json[2600];
    int length = snprintf(json, sizeof(json),
        "{\"can_mqtt\":%s,\"can_ethernet\":%s,\"gateway_id\":\"%s\","
        "\"company_id\":\"%s\",\"site_id\":\"%s\",\"warehouse_id\":\"%s\","
        "\"warehouse_name\":\"%s\",\"topic_json\":\"%s\",\"topic_bits\":\"%s\","
        "\"wifi_dhcp\":%u,\"wifi_ip\":\"%s\","
        "\"wifi_netmask\":\"%s\",\"wifi_gateway\":\"%s\",\"wifi_dns\":\"%s\","
        "\"eth_router_mode\":%u,\"eth_dhcp\":%u,\"eth_ip\":\"%s\","
        "\"eth_netmask\":\"%s\",\"eth_gateway\":\"%s\",\"eth_dns\":\"%s\","
        "\"mqtt_broker\":\"%s\",\"mqtt_port\":%u,\"mqtt_transport\":\"%s\","
        "\"mqtt_user\":\"%s\",\"publish_interval_ms\":%u,"
        "\"sntp_primary\":\"%s\",\"sntp_fallback\":\"%s\",\"timezone\":\"%s\",\"wifi_profiles\":[",
        it ? "true" : "false", ethernet ? "true" : "false",
        gateway_id, company_id, site_id, warehouse_id, warehouse_name,
        topics.status_json, topics.status_bits,
        config.wifi_dhcp, config.wifi_ip, config.wifi_netmask,
        config.wifi_gateway, config.wifi_dns, config.eth_router_mode, config.eth_dhcp,
        config.eth_ip, config.eth_netmask, config.eth_gateway, config.eth_dns,
        mqtt_broker, config.mqtt_port,
        config.mqtt_transport == GATEWAY_MQTT_TLS ? "tls" : "tcp",
        mqtt_user, config.publish_interval_ms, ntp_primary, ntp_fallback, timezone);
    for (uint8_t i = 0; i < config.wifi_profile_count && length < (int)sizeof(json) - 128; ++i) {
        char ssid[72];
        json_escape(ssid, sizeof(ssid), config.wifi_profiles[i].ssid);
        const bool active = wifi.connected && !strcmp(wifi.ssid, config.wifi_profiles[i].ssid);
        length += snprintf(json + length, sizeof(json) - length,
                           "%s{\"ssid\":\"%s\",\"active\":%s}",
                           i ? "," : "", ssid, active ? "true" : "false");
    }
    length += snprintf(json + length, sizeof(json) - length, "]}");
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, length);
}

static void apply_saved_config_task(void *argument)
{
    const uint32_t flags = (uint32_t)(uintptr_t)argument;
    vTaskDelay(pdMS_TO_TICKS(350));
    gateway_config_t config;
    gateway_config_get(&config);
    if ((flags & APPLY_MQTT) != 0U) gateway_mqtt_reconfigure();
    if ((flags & APPLY_NETWORK) != 0U) (void)gateway_network_apply(&config);
    if ((flags & APPLY_TIME) != 0U) {
        const platform_time_config_t time_config = {
            .primary_server = config.sntp_primary,
            .fallback_server = config.sntp_fallback,
            .timezone = config.timezone,
        };
        (void)platform_time_reconfigure(&time_config);
    }
    vTaskDelete(NULL);
}

static bool wifi_config_changed(const gateway_config_t *before,
                                const gateway_config_t *after)
{
    if (before->wifi_profile_count != after->wifi_profile_count) return true;
    for (uint8_t i = 0U; i < before->wifi_profile_count; ++i) {
        if (strcmp(before->wifi_profiles[i].ssid, after->wifi_profiles[i].ssid) != 0 ||
            strcmp(before->wifi_profiles[i].password, after->wifi_profiles[i].password) != 0) {
            return true;
        }
    }
    return before->wifi_dhcp != after->wifi_dhcp ||
           strcmp(before->wifi_ip, after->wifi_ip) != 0 ||
           strcmp(before->wifi_netmask, after->wifi_netmask) != 0 ||
           strcmp(before->wifi_gateway, after->wifi_gateway) != 0 ||
           strcmp(before->wifi_dns, after->wifi_dns) != 0;
}

static bool ethernet_config_changed(const gateway_config_t *before,
                                    const gateway_config_t *after)
{
    return before->eth_router_mode != after->eth_router_mode ||
           before->eth_dhcp != after->eth_dhcp ||
           strcmp(before->eth_ip, after->eth_ip) != 0 ||
           strcmp(before->eth_netmask, after->eth_netmask) != 0 ||
           strcmp(before->eth_gateway, after->eth_gateway) != 0 ||
           strcmp(before->eth_dns, after->eth_dns) != 0;
}

static bool mqtt_config_changed(const gateway_config_t *before,
                                const gateway_config_t *after)
{
    return strcmp(before->gateway_id, after->gateway_id) != 0 ||
           strcmp(before->company_id, after->company_id) != 0 ||
           strcmp(before->site_id, after->site_id) != 0 ||
           strcmp(before->warehouse_id, after->warehouse_id) != 0 ||
           strcmp(before->mqtt_broker, after->mqtt_broker) != 0 ||
           before->mqtt_port != after->mqtt_port ||
           before->mqtt_transport != after->mqtt_transport ||
           strcmp(before->mqtt_user, after->mqtt_user) != 0 ||
           strcmp(before->mqtt_password, after->mqtt_password) != 0 ||
           before->publish_interval_ms != after->publish_interval_ms;
}

static bool time_config_changed(const gateway_config_t *before,
                                const gateway_config_t *after)
{
    return strcmp(before->sntp_primary, after->sntp_primary) != 0 ||
           strcmp(before->sntp_fallback, after->sntp_fallback) != 0 ||
           strcmp(before->timezone, after->timezone) != 0;
}

static esp_err_t config_post(httpd_req_t *request)
{
    gateway_auth_session_t session;
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, &session)) return ESP_OK;
    const bool can_mqtt = gateway_auth_role_has_permission(session.role, GW_PERMISSION_MQTT_CONFIG);
    const bool can_ethernet = gateway_auth_role_has_permission(session.role,
                                                                GW_PERMISSION_ETHERNET_CONFIG);
    char body[1024] = {0}, value[128];
    if (read_body(request, body, sizeof(body)) != ESP_OK)
        return gateway_web_send_text(request, "400 Bad Request", "Dữ liệu quá dài");
    gateway_config_t config;
    gateway_config_get(&config);
    const gateway_config_t previous = config;
#define SET_STRING(key, member) do { if (field(body, key, value, sizeof(value)) && value[0]) \
    strlcpy(config.member, value, sizeof(config.member)); } while (0)
    SET_STRING("gateway_id", gateway_id);
    SET_STRING("wifi_ip", wifi_ip);
    SET_STRING("wifi_netmask", wifi_netmask);
    SET_STRING("wifi_gateway", wifi_gateway);
    SET_STRING("wifi_dns", wifi_dns);
    if (can_ethernet) {
        SET_STRING("eth_ip", eth_ip);
        SET_STRING("eth_netmask", eth_netmask);
        SET_STRING("eth_gateway", eth_gateway);
        SET_STRING("eth_dns", eth_dns);
    }
    if (can_mqtt) {
        if (field(body, "company_id", value, sizeof(value))) {
            if (!gateway_topic_segment_valid(value))
                return gateway_web_send_text(request, "400 Bad Request",
                    "Mã công ty không hợp lệ: chỉ dùng chữ thường, số, dấu - hoặc _");
            strlcpy(config.company_id, value, sizeof(config.company_id));
        }
        if (field(body, "site_id", value, sizeof(value))) {
            if (!gateway_topic_segment_valid(value))
                return gateway_web_send_text(request, "400 Bad Request",
                    "Mã địa điểm không hợp lệ: chỉ dùng chữ thường, số, dấu - hoặc _");
            strlcpy(config.site_id, value, sizeof(config.site_id));
        }
        SET_STRING("mqtt_broker", mqtt_broker);
        SET_STRING("mqtt_user", mqtt_user);
        SET_STRING("sntp_primary", sntp_primary);
        SET_STRING("sntp_fallback", sntp_fallback);
        SET_STRING("timezone", timezone);
    }
#undef SET_STRING
    if (can_mqtt && field(body, "mqtt_password", value, sizeof(value)) && value[0])
        strlcpy(config.mqtt_password, value, sizeof(config.mqtt_password));
    if (field(body, "wifi_dhcp", value, sizeof(value))) config.wifi_dhcp = atoi(value) != 0;
    if (can_ethernet && field(body, "eth_router_mode", value, sizeof(value))) config.eth_router_mode = atoi(value) != 0;
    if (can_ethernet && field(body, "eth_dhcp", value, sizeof(value))) config.eth_dhcp = atoi(value) != 0;
    if (can_mqtt && field(body, "mqtt_port", value, sizeof(value))) config.mqtt_port = (uint16_t)atoi(value);
    if (can_mqtt && field(body, "publish_interval_ms", value, sizeof(value))) config.publish_interval_ms = (uint16_t)atoi(value);
    if (can_mqtt && field(body, "mqtt_transport", value, sizeof(value)))
        config.mqtt_transport = strcmp(value, "tls") == 0 ? GATEWAY_MQTT_TLS : GATEWAY_MQTT_TCP;
    char ssid[33] = {0}, password[64] = {0};
    const bool ssid_present = field(body, "wifi_ssid", ssid, sizeof(ssid));
    const bool password_present = field(body, "wifi_password", password, sizeof(password));
    if (ssid_present && ssid[0]) {
        const char *selected_password = password;
        if (!password_present || !password[0]) {
            selected_password = NULL;
            for (uint8_t i = 0; i < config.wifi_profile_count; ++i) {
                if (!strcmp(config.wifi_profiles[i].ssid, ssid)) {
                    selected_password = config.wifi_profiles[i].password;
                    break;
                }
            }
            if (!selected_password)
                return gateway_web_send_text(request, "400 Bad Request",
                                             "Mạng WiFi mới cần mật khẩu");
        }
        if (!gateway_config_add_wifi(&config, ssid, selected_password))
            return gateway_web_send_text(request, "400 Bad Request",
                                         "Mạng WiFi không hợp lệ");
    }
    if (!gateway_config_gateway_id_valid(config.gateway_id))
        return gateway_web_send_text(request, "400 Bad Request",
            "Mã Gateway chỉ được dùng chữ, số, dấu - hoặc _ (tối đa 16 ký tự)");
    if (!gateway_config_derive_warehouse_identity(&config))
        return gateway_web_send_text(request, "400 Bad Request",
                                     "Không thể tạo định danh từ Mã Gateway");
    if (can_mqtt && config.mqtt_port == 0U)
        return gateway_web_send_text(request, "400 Bad Request", "Cổng MQTT không hợp lệ");
    if (can_mqtt && (config.publish_interval_ms < 250U ||
                     config.publish_interval_ms > 60000U))
        return gateway_web_send_text(request, "400 Bad Request",
                                     "Chu kỳ gửi MQTT phải từ 250 đến 60000 ms");

    const esp_err_t error = gateway_config_save(&config);
    if (error == ESP_ERR_INVALID_ARG)
        return gateway_web_send_text(request, "400 Bad Request", "Cấu hình không hợp lệ");
    if (error == ESP_ERR_INVALID_STATE)
        return gateway_web_send_text(request, "409 Conflict",
            "Định danh MQTT cũ đang được dọn trên máy chủ; hãy thử đổi Mã Gateway sau khi MQTT online");
    if (error != ESP_OK)
        return gateway_web_send_text(request, "500 Internal Server Error",
                                     "Không thể lưu cấu hình vào bộ nhớ");

    uint32_t apply_flags = 0U;
    if (strcmp(previous.gateway_id, config.gateway_id) != 0 ||
        wifi_config_changed(&previous, &config) ||
        ethernet_config_changed(&previous, &config)) apply_flags |= APPLY_NETWORK;
    if (mqtt_config_changed(&previous, &config)) apply_flags |= APPLY_MQTT;
    if (time_config_changed(&previous, &config)) apply_flags |= APPLY_TIME;
    if (apply_flags != 0U &&
        xTaskCreate(apply_saved_config_task, "gw_cfg_apply", 4096,
                    (void *)(uintptr_t)apply_flags, 5, NULL) != pdPASS)
        return gateway_web_send_text(request, "503 Service Unavailable",
                                     "Đã lưu nhưng chưa thể áp dụng ngay");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t wifi_profile_delete(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, NULL)) return ESP_OK;
    char body[128] = {0}, ssid[33] = {0};
    if (read_body(request, body, sizeof(body)) != ESP_OK ||
        !field(body, "ssid", ssid, sizeof(ssid)) || !ssid[0])
        return gateway_web_send_text(request, "400 Bad Request", "Thiếu tên mạng WiFi");

    gateway_config_t config;
    gateway_config_get(&config);
    if (!gateway_config_remove_wifi(&config, ssid))
        return gateway_web_send_text(request, "404 Not Found", "Không tìm thấy mạng WiFi");
    if (gateway_config_save(&config) != ESP_OK)
        return gateway_web_send_text(request, "500 Internal Server Error",
                                     "Không thể lưu danh sách WiFi");
    if (xTaskCreate(apply_saved_config_task, "gw_cfg_apply", 4096,
                    (void *)(uintptr_t)APPLY_NETWORK, 5, NULL) != pdPASS)
        return gateway_web_send_text(request, "503 Service Unavailable",
                                     "Đã xóa nhưng chưa thể áp dụng ngay");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t scan(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, NULL)) return ESP_OK;
    platform_wifi_scan_record_t networks[20];
    uint16_t count = 20U;
    const platform_wifi_scan_config_t config = {
        .show_hidden = false, .active_min_ms = 60, .active_max_ms = 120
    };
    if (platform_wifi_scan(&config, networks, &count) != ESP_OK)
        return gateway_web_send_text(request, "503 Service Unavailable", "WiFi đang bận");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_send_chunk(request, "{\"networks\":[", 13U);
    char json[96];
    for (uint16_t i = 0; i < count; ++i) {
        char ssid[72];
        json_escape(ssid, sizeof(ssid), networks[i].ssid);
        const int length = snprintf(json, sizeof(json), "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                                    i ? "," : "", ssid, networks[i].rssi);
        httpd_resp_send_chunk(request, json, length);
    }
    httpd_resp_send_chunk(request, "]}", 2U);
    return httpd_resp_send_chunk(request, NULL, 0U);
}

static esp_err_t status(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, NULL)) return ESP_OK;
    gateway_config_t config = {0};
    platform_wifi_sta_status_t wifi = {0};
    bsp_eth_status_t ethernet = {0};
    gateway_config_get(&config);
    platform_wifi_get_sta_status(&wifi);
    bsp_eth_get_status(&ethernet);
    const bool eth_debug_mode = !config.eth_router_mode;
    const char *eth_ip = ethernet.ip[0] != '\0' ? ethernet.ip
                                                  : (eth_debug_mode ? GATEWAY_ETH_DEBUG_IP : "");
    char wifi_ssid[72];
    json_escape(wifi_ssid, sizeof(wifi_ssid), wifi.ssid);
    char json[720];
    const int length = snprintf(json, sizeof(json),
        "{\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
        "\"ethernet\":{\"connected\":%s,\"link\":%s,\"ip\":\"%s\",\"mode\":\"%s\","
        "\"uplink\":%s,\"debug\":%s},"
        "\"production_network\":%s,\"mqtt\":%s,\"ap\":%s,\"ap_ip\":\"%s\",\"ap_manual\":%s,"
        "\"time_valid\":%s,\"unix_time\":%lld}",
        wifi.connected ? "true" : "false", wifi_ssid, wifi.ip, wifi.rssi,
        ethernet.connected ? "true" : "false",
        bsp_eth_link_is_up() ? "true" : "false", eth_ip,
        eth_debug_mode ? "DEBUG" : "UPLINK",
        gateway_network_eth_uplink_available() ? "true" : "false",
        gateway_network_eth_debug_active() ? "true" : "false",
        gateway_network_production_available() ? "true" : "false",
        gateway_mqtt_is_connected() ? "true" : "false",
        platform_wifi_ap_is_active() ? "true" : "false",
        GATEWAY_AP_IP,
        gateway_network_ap_is_manual() ? "true" : "false",
        platform_time_is_valid((time_t)VALID_UNIX_TIME) ? "true" : "false",
        (long long)time(NULL));
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json, length);
}

static esp_err_t ap_control(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, NULL)) return ESP_OK;
    char body[32] = {0}, enabled[8] = {0};
    if (read_body(request, body, sizeof(body)) != ESP_OK ||
        !field(body, "enabled", enabled, sizeof(enabled)))
        return gateway_web_send_text(request, "400 Bad Request", "Thiếu trạng thái AP");
    if (gateway_network_set_ap(atoi(enabled) != 0) != ESP_OK)
        return gateway_web_send_text(request, "500 Internal Server Error",
                                     "Không thể đổi trạng thái AP");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

esp_err_t gateway_config_http_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri="/app/it",.method=HTTP_GET,.handler=page},
        {.uri="/cau-hinh",.method=HTTP_GET,.handler=page},
        {.uri="/dang-nhap",.method=HTTP_GET,.handler=legacy_login},
        {.uri="/dang-xuat",.method=HTTP_GET,.handler=legacy_login},
        {.uri="/api/it/config",.method=HTTP_GET,.handler=config_get},
        {.uri="/api/it/config",.method=HTTP_POST,.handler=config_post},
        {.uri="/api/it/wifi-scan",.method=HTTP_GET,.handler=scan},
        {.uri="/api/it/wifi-profiles/delete",.method=HTTP_POST,.handler=wifi_profile_delete},
        {.uri="/api/it/status",.method=HTTP_GET,.handler=status},
        {.uri="/api/it/ap",.method=HTTP_POST,.handler=ap_control},
        {.uri="/api/gateway/config",.method=HTTP_GET,.handler=config_get},
        {.uri="/api/gateway/config",.method=HTTP_POST,.handler=config_post},
        {.uri="/api/gateway/wifi-scan",.method=HTTP_GET,.handler=scan},
        {.uri="/api/gateway/wifi-profiles/delete",.method=HTTP_POST,.handler=wifi_profile_delete},
        {.uri="/api/gateway/status",.method=HTTP_GET,.handler=status},
        {.uri="/api/gateway/ap",.method=HTTP_POST,.handler=ap_control},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const esp_err_t error = httpd_register_uri_handler(server, &handlers[i]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
