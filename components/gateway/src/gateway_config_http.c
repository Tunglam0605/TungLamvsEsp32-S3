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
#include "gateway_web_theme.h"
#include "platform_wifi.h"
#include "platform_time.h"
#include <time.h>

#define VALID_UNIX_TIME 1704067200LL

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
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, NULL)) return ESP_OK;
    gateway_config_t config;
    gateway_config_get(&config);
    char gateway_id[40], mqtt_broker[192], mqtt_user[96];
    char ntp_primary[128], ntp_fallback[128], timezone[128];
    json_escape(gateway_id, sizeof(gateway_id), config.gateway_id);
    json_escape(mqtt_broker, sizeof(mqtt_broker), config.mqtt_broker);
    json_escape(mqtt_user, sizeof(mqtt_user), config.mqtt_user);
    json_escape(ntp_primary, sizeof(ntp_primary), config.sntp_primary);
    json_escape(ntp_fallback, sizeof(ntp_fallback), config.sntp_fallback);
    json_escape(timezone, sizeof(timezone), config.timezone);
    char json[1800];
    int length = snprintf(json, sizeof(json),
        "{\"gateway_id\":\"%s\",\"wifi_dhcp\":%u,\"wifi_ip\":\"%s\","
        "\"wifi_netmask\":\"%s\",\"wifi_gateway\":\"%s\",\"wifi_dns\":\"%s\","
        "\"eth_router_mode\":%u,\"eth_dhcp\":%u,\"eth_ip\":\"%s\","
        "\"eth_netmask\":\"%s\",\"eth_gateway\":\"%s\",\"eth_dns\":\"%s\","
        "\"mqtt_broker\":\"%s\",\"mqtt_port\":%u,\"mqtt_transport\":\"%s\","
        "\"mqtt_user\":\"%s\",\"publish_interval_ms\":%u,"
        "\"sntp_primary\":\"%s\",\"sntp_fallback\":\"%s\",\"timezone\":\"%s\",\"wifi_profiles\":[",
        gateway_id, config.wifi_dhcp, config.wifi_ip, config.wifi_netmask,
        config.wifi_gateway, config.wifi_dns, config.eth_router_mode, config.eth_dhcp,
        config.eth_ip, config.eth_netmask, config.eth_gateway, config.eth_dns,
        mqtt_broker, config.mqtt_port,
        config.mqtt_transport == GATEWAY_MQTT_TLS ? "tls" : "tcp",
        mqtt_user, config.publish_interval_ms, ntp_primary, ntp_fallback, timezone);
    for (uint8_t i = 0; i < config.wifi_profile_count && length < (int)sizeof(json) - 80; ++i) {
        char ssid[72];
        json_escape(ssid, sizeof(ssid), config.wifi_profiles[i].ssid);
        length += snprintf(json + length, sizeof(json) - length, "%s\"%s\"",
                           i ? "," : "", ssid);
    }
    length += snprintf(json + length, sizeof(json) - length, "]}");
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, length);
}

static void apply_saved_config_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(350));
    gateway_config_t config;
    gateway_config_get(&config);
    (void)gateway_network_apply(&config);
    gateway_mqtt_reconfigure();
    const platform_time_config_t time_config = {
        .primary_server = config.sntp_primary,
        .fallback_server = config.sntp_fallback,
        .timezone = config.timezone,
    };
    (void)platform_time_reconfigure(&time_config);
    vTaskDelete(NULL);
}

static esp_err_t config_post(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_NETWORK_CONFIG, NULL)) return ESP_OK;
    char body[1024] = {0}, value[128];
    if (read_body(request, body, sizeof(body)) != ESP_OK)
        return httpd_resp_send_err(request, 400, "Dữ liệu quá dài");
    gateway_config_t config;
    gateway_config_get(&config);
#define SET_STRING(key, member) do { if (field(body, key, value, sizeof(value)) && value[0]) \
    strlcpy(config.member, value, sizeof(config.member)); } while (0)
    SET_STRING("gateway_id", gateway_id);
    SET_STRING("ap_password", ap_password);
    SET_STRING("wifi_ip", wifi_ip);
    SET_STRING("wifi_netmask", wifi_netmask);
    SET_STRING("wifi_gateway", wifi_gateway);
    SET_STRING("wifi_dns", wifi_dns);
    SET_STRING("eth_ip", eth_ip);
    SET_STRING("eth_netmask", eth_netmask);
    SET_STRING("eth_gateway", eth_gateway);
    SET_STRING("eth_dns", eth_dns);
    SET_STRING("mqtt_broker", mqtt_broker);
    SET_STRING("mqtt_user", mqtt_user);
    SET_STRING("sntp_primary", sntp_primary);
    SET_STRING("sntp_fallback", sntp_fallback);
    SET_STRING("timezone", timezone);
#undef SET_STRING
    if (field(body, "mqtt_password", value, sizeof(value)) && value[0])
        strlcpy(config.mqtt_password, value, sizeof(config.mqtt_password));
    if (field(body, "wifi_dhcp", value, sizeof(value))) config.wifi_dhcp = atoi(value) != 0;
    if (field(body, "eth_router_mode", value, sizeof(value))) config.eth_router_mode = atoi(value) != 0;
    if (field(body, "eth_dhcp", value, sizeof(value))) config.eth_dhcp = atoi(value) != 0;
    if (field(body, "mqtt_port", value, sizeof(value))) config.mqtt_port = (uint16_t)atoi(value);
    if (field(body, "publish_interval_ms", value, sizeof(value))) config.publish_interval_ms = (uint16_t)atoi(value);
    if (field(body, "mqtt_transport", value, sizeof(value)))
        config.mqtt_transport = strcmp(value, "tls") == 0 ? GATEWAY_MQTT_TLS : GATEWAY_MQTT_TCP;
    char ssid[33] = {0}, password[64] = {0};
    if (field(body, "wifi_ssid", ssid, sizeof(ssid)) && ssid[0] &&
        field(body, "wifi_password", password, sizeof(password)) && password[0])
        (void)gateway_config_add_wifi(&config, ssid, password);
    const esp_err_t error = gateway_config_save(&config);
    if (error != ESP_OK) return httpd_resp_send_err(request, 400, "Cấu hình không hợp lệ");
    if (xTaskCreate(apply_saved_config_task, "gw_cfg_apply", 4096, NULL, 5, NULL) != pdPASS)
        return httpd_resp_send_err(request, 503, "Đã lưu nhưng chưa thể áp dụng ngay");
    httpd_resp_set_type(request, "application/json");
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
        return httpd_resp_send_err(request, 503, "WiFi đang bận");
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
    platform_wifi_sta_status_t wifi = {0};
    bsp_eth_status_t ethernet = {0};
    platform_wifi_get_sta_status(&wifi);
    bsp_eth_get_status(&ethernet);
    char wifi_ssid[72];
    json_escape(wifi_ssid, sizeof(wifi_ssid), wifi.ssid);
    char json[560];
    const int length = snprintf(json, sizeof(json),
        "{\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
        "\"ethernet\":{\"connected\":%s,\"ip\":\"%s\",\"uplink\":%s,\"debug\":%s},"
        "\"production_network\":%s,\"mqtt\":%s,\"ap\":%s,\"ap_manual\":%s,"
        "\"time_valid\":%s,\"unix_time\":%lld}",
        wifi.connected ? "true" : "false", wifi_ssid, wifi.ip, wifi.rssi,
        ethernet.connected ? "true" : "false", ethernet.ip,
        gateway_network_eth_uplink_available() ? "true" : "false",
        gateway_network_eth_debug_active() ? "true" : "false",
        gateway_network_production_available() ? "true" : "false",
        gateway_mqtt_is_connected() ? "true" : "false",
        platform_wifi_ap_is_active() ? "true" : "false",
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
        return httpd_resp_send_err(request, 400, "Thiếu trạng thái AP");
    if (gateway_network_set_ap(atoi(enabled) != 0) != ESP_OK)
        return httpd_resp_send_err(request, 500, "Không thể đổi trạng thái AP");
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
        {.uri="/api/it/status",.method=HTTP_GET,.handler=status},
        {.uri="/api/it/ap",.method=HTTP_POST,.handler=ap_control},
        {.uri="/api/gateway/config",.method=HTTP_GET,.handler=config_get},
        {.uri="/api/gateway/config",.method=HTTP_POST,.handler=config_post},
        {.uri="/api/gateway/wifi-scan",.method=HTTP_GET,.handler=scan},
        {.uri="/api/gateway/status",.method=HTTP_GET,.handler=status},
        {.uri="/api/gateway/ap",.method=HTTP_POST,.handler=ap_control},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const esp_err_t error = httpd_register_uri_handler(server, &handlers[i]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
