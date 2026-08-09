/**
 * @file    platform_wifi.c
 * @brief   Provider Wi-Fi nền tảng: driver + netif + event bridge của ESP-IDF.
 *
 *          Module này sở hữu MỌI provider mechanics của ESP-IDF Wi-Fi và
 *          KHÔNG biết policy của product (xem platform_wifi.h).
 *
 *          ═══ TRẠNG THÁI FACTUAL (single source of truth) ═══
 *          - s_sta_connected: STA nhận IP (IP_EVENT_STA_GOT_IP)
 *          - s_ap_started: AP chạy (WIFI_EVENT_AP_START)
 *          - s_ap_client_count: client AP (AP_STACONNECTED/DISCONNECTED,
 *            giảm an toàn không underflow)
 *          Caller đọc qua platform_wifi_sta_is_connected() /
 *          platform_wifi_ap_is_active() / platform_wifi_ap_client_count().
 *
 *          ═══ EVENT BRIDGE ═══
 *          ESP-IDF event handler (WIFI_EVENT + IP_EVENT_STA_GOT_IP) chuyển
 *          sang platform_wifi_event_t rồi gọi callback product (policy).
 *          AP_STARTED được dùng để đặt s_ap_started = true (không fake
 *          active trước provider setup thành công — xem §30 spec E.3.1).
 *
 *          ═══ SINGLETON ═══
 *          ESP-IDF Wi-Fi là singleton hệ thống → static state là hợp lệ,
 *          deterministic, không cấp phát heap tùy ý.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#include "platform_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

#define PLATFORM_WIFI_MAX_SSID_LEN 33
#define PLATFORM_WIFI_MAX_PASSWORD_LEN 64
#define PLATFORM_WIFI_MAX_IP_LEN 16
#define PLATFORM_WIFI_MAX_AP_CLIENTS 4
#define PLATFORM_WIFI_MAX_SCAN_STATIC 48

static const char *TAG = "PLATFORM_WIFI";

/* AP identity được sao chép khi start_apsta (không giữ pointer của caller). */
static char s_ap_ssid[PLATFORM_WIFI_MAX_SSID_LEN];
static char s_ap_password[PLATFORM_WIFI_MAX_PASSWORD_LEN];
static char s_ap_ip[PLATFORM_WIFI_MAX_IP_LEN];
static char s_ap_netmask[PLATFORM_WIFI_MAX_IP_LEN];
static uint8_t s_ap_channel;
static uint8_t s_ap_max_clients;

/* Credentials STA hiện tại (bản sao, không giữ pointer của caller). */
static char s_sta_ssid[PLATFORM_WIFI_MAX_SSID_LEN];
static char s_sta_password[PLATFORM_WIFI_MAX_PASSWORD_LEN];

/* Trạng thái factual của provider (single source of truth). */
static bool s_started;
static bool s_sta_connected;
static bool s_ap_started;
static volatile uint8_t s_ap_client_count;

/* Bộ đệm quét tĩnh: wifi_ap_record_t lớn, không để trên stack task caller. */
static wifi_ap_record_t s_scan_records[PLATFORM_WIFI_MAX_SCAN_STATIC];

static platform_wifi_event_callback_t s_event_callback;
static void *s_event_context;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    strncpy(dst, src ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool parse_ipv4(const char *text, esp_ip4_addr_t *address)
{
    return text && text[0] && address && esp_netif_str_to_ip4(text, address) == ESP_OK;
}

static void bridge_event(platform_wifi_event_t event)
{
    if (s_event_callback) s_event_callback(event, s_event_context);
}

/* Map authmode ESP-IDF → platform_wifi_auth_mode_t — map TƯỜNG MINH mọi giá
 * trị wifi_auth_mode_t v6.1 (xem header platform_wifi.h về hợp đồng số 0..17
 * với JSON "auth" của portal). Mọi giá trị provider ngoài hợp đồng → UNMAPPED.
 * Lưu ý: WIFI_AUTH_WPA2_ENTERPRISE là alias == WIFI_AUTH_ENTERPRISE (cùng
 * giá trị 5) nên không tạo case riêng (lỗi duplicate case). */
static platform_wifi_auth_mode_t map_auth_mode(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:            return PLATFORM_WIFI_AUTH_OPEN;
    case WIFI_AUTH_WEP:             return PLATFORM_WIFI_AUTH_WEP;
    case WIFI_AUTH_WPA_PSK:         return PLATFORM_WIFI_AUTH_WPA_PSK;
    case WIFI_AUTH_WPA2_PSK:        return PLATFORM_WIFI_AUTH_WPA2_PSK;
    case WIFI_AUTH_WPA_WPA2_PSK:    return PLATFORM_WIFI_AUTH_WPA_WPA2_PSK;
    case WIFI_AUTH_ENTERPRISE:      return PLATFORM_WIFI_AUTH_ENTERPRISE;
    case WIFI_AUTH_WPA3_PSK:        return PLATFORM_WIFI_AUTH_WPA3_PSK;
    case WIFI_AUTH_WPA2_WPA3_PSK:   return PLATFORM_WIFI_AUTH_WPA2_WPA3_PSK;
    case WIFI_AUTH_WAPI_PSK:        return PLATFORM_WIFI_AUTH_WAPI_PSK;
    case WIFI_AUTH_OWE:             return PLATFORM_WIFI_AUTH_OWE;
    case WIFI_AUTH_WPA3_ENT_192:    return PLATFORM_WIFI_AUTH_WPA3_ENT_192;
    case WIFI_AUTH_DUMMY_1:         return PLATFORM_WIFI_AUTH_RESERVED_11;
    case WIFI_AUTH_DUMMY_2:         return PLATFORM_WIFI_AUTH_RESERVED_12;
    case WIFI_AUTH_DPP:             return PLATFORM_WIFI_AUTH_DPP;
    case WIFI_AUTH_WPA3_ENTERPRISE: return PLATFORM_WIFI_AUTH_WPA3_ENTERPRISE;
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return PLATFORM_WIFI_AUTH_WPA2_WPA3_ENTERPRISE;
    case WIFI_AUTH_WPA_ENTERPRISE:  return PLATFORM_WIFI_AUTH_WPA_ENTERPRISE;
    case WIFI_AUTH_MAX:             return PLATFORM_WIFI_AUTH_UNKNOWN;
    default:                        return PLATFORM_WIFI_AUTH_UNMAPPED;
    }
}

/* Đảm bảo driver đang ở mode có khả năng quét STA (provider mechanics):
 * AP-only → APSTA + ~50 ms; NULL → STA + ~50 ms; đã chứa STA → không đổi. */
static esp_err_t ensure_scan_capable_mode(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) return err;

    if ((mode & WIFI_MODE_STA) != 0) return ESP_OK;
    if (mode == WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    } else if (mode == WIFI_MODE_NULL) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

/* Gán IP tĩnh cho netif STA: dừng DHCP client → set ip_info → set DNS chính.
 * Lỗi DHCP_ALREADY_STOPPED được chấp nhận (behavior giữ nguyên từ wifi_init). */
static esp_err_t apply_sta_network_config(const platform_wifi_sta_network_config_t *network)
{
    if (!s_sta_netif) return ESP_ERR_INVALID_STATE;
    if (!network || network->dhcp) return ESP_OK;

    if (!parse_ipv4(network->ip, &(esp_ip4_addr_t){0}) ||
        !parse_ipv4(network->netmask, &(esp_ip4_addr_t){0}) ||
        !parse_ipv4(network->gateway, &(esp_ip4_addr_t){0}) ||
        !parse_ipv4(network->dns, &(esp_ip4_addr_t){0})) {
        ESP_LOGE(TAG, "Static IP mode requires valid IP, netmask, gateway and DNS");
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_ip_info_t ip_info = { 0 };
    esp_err_t err = esp_netif_str_to_ip4(network->ip, &ip_info.ip);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(network->netmask, &ip_info.netmask);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(network->gateway, &ip_info.gw);
    if (err != ESP_OK) return err;

    err = esp_netif_dhcpc_stop(s_sta_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    err = esp_netif_set_ip_info(s_sta_netif, &ip_info);
    if (err != ESP_OK) return err;

    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    err = esp_netif_str_to_ip4(network->dns, &dns.ip.u_addr.ip4);
    if (err == ESP_OK) err = esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "STA network: static %s (gateway=%s)", network->ip, network->gateway);
    }
    return err;
}

/* Cấu hình netif SoftAP: IP cố định + DHCP server, giữ error tolerance
 * already-stopped/already-started như cũ. Giá trị IP/netmask từ caller. */
static esp_err_t configure_ap_netif(void)
{
    if (!s_ap_netif) return ESP_ERR_INVALID_STATE;

    esp_netif_ip_info_t ip_info = { 0 };
    esp_err_t err = esp_netif_str_to_ip4(s_ap_ip, &ip_info.ip);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(s_ap_ip, &ip_info.gw);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(s_ap_netmask, &ip_info.netmask);
    if (err != ESP_OK) return err;

    err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
    if (err != ESP_OK) return err;
    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return err;

    ESP_LOGI(TAG, "Configuration AP network: %s/%s, DHCP server enabled",
             s_ap_ip, s_ap_netmask);
    return ESP_OK;
}

/* ESP-IDF event handler — private, chỉ được đăng ký nội bộ. */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            s_sta_connected = false;
            ESP_LOGI(TAG, "STA started");
            bridge_event(PLATFORM_WIFI_EVENT_STA_STARTED);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_sta_connected = false;
            ESP_LOGI(TAG, "STA disconnected");
            bridge_event(PLATFORM_WIFI_EVENT_STA_DISCONNECTED);
            break;
        case WIFI_EVENT_AP_START:
            s_ap_started = true;
            ESP_LOGI(TAG, "AP started: %s", s_ap_ssid);
            bridge_event(PLATFORM_WIFI_EVENT_AP_STARTED);
            break;
        case WIFI_EVENT_AP_STOP:
            s_ap_started = false;
            s_ap_client_count = 0;
            ESP_LOGW(TAG, "AP stopped");
            bridge_event(PLATFORM_WIFI_EVENT_AP_STOPPED);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            if (s_ap_client_count < 255) s_ap_client_count++;
            ESP_LOGI(TAG, "AP client connected (count=%u)", s_ap_client_count);
            bridge_event(PLATFORM_WIFI_EVENT_AP_CLIENT_CONNECTED);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            /* Giảm an toàn: không underflow khi event bất thường */
            if (s_ap_client_count > 0) s_ap_client_count--;
            ESP_LOGI(TAG, "AP client disconnected (count=%u)", s_ap_client_count);
            bridge_event(PLATFORM_WIFI_EVENT_AP_CLIENT_DISCONNECTED);
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_sta_connected = true;
        ESP_LOGI(TAG, "STA connected, got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        bridge_event(PLATFORM_WIFI_EVENT_STA_GOT_IP);
    }
}

esp_err_t platform_wifi_start_apsta(const platform_wifi_sta_network_config_t *sta_network,
                                    const platform_wifi_ap_config_t *ap_config,
                                    platform_wifi_event_callback_t callback,
                                    void *context)
{
    if (!ap_config) return ESP_ERR_INVALID_ARG;

    /* Sao chép AP identity + network config (không giữ pointer của caller). */
    copy_string(s_ap_ssid, sizeof(s_ap_ssid), ap_config->ssid);
    copy_string(s_ap_password, sizeof(s_ap_password), ap_config->password);
    copy_string(s_ap_ip, sizeof(s_ap_ip), ap_config->ip);
    copy_string(s_ap_netmask, sizeof(s_ap_netmask), ap_config->netmask);
    s_ap_channel = ap_config->channel ? ap_config->channel : 1;
    s_ap_max_clients = ap_config->max_clients ? ap_config->max_clients : PLATFORM_WIFI_MAX_AP_CLIENTS;
    s_event_callback = callback;
    s_event_context = context;

    /* Global network core: giữ thứ tự gọi sớm (W5500 BSP phụ thuộc) —
     * chấp nhận ESP_ERR_INVALID_STATE khi đã init trước đó. */
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    /* Tạo 2 netif: STA + AP */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;

    /* Cấu hình IP: AP cố định; STA theo DHCP hoặc IP tĩnh */
    ret = configure_ap_netif();
    if (ret != ESP_OK) return ret;
    ret = apply_sta_network_config(sta_network);
    if (ret != ESP_OK) return ret;

    /* Khởi tạo driver Wi-Fi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) return ret;

    /* Đăng ký event handler: toàn bộ WIFI_EVENT + IP_EVENT_STA_GOT_IP */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) return ret;
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) return ret;

    /* Reset trạng thái và bật APSTA: AP cấu hình mở ngay, STA song song */
    s_sta_connected = false;
    s_ap_started = false;
    s_ap_client_count = 0;
    s_started = true;
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret == ESP_OK) ret = platform_wifi_ap_start();
    if (ret == ESP_OK) ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STOPPED) {
        s_started = false;
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi provider started in APSTA mode");
    return ESP_OK;
}

esp_err_t platform_wifi_sta_set_credentials(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;
    copy_string(s_sta_ssid, sizeof(s_sta_ssid), ssid);
    copy_string(s_sta_password, sizeof(s_sta_password), password);
    return ESP_OK;
}

esp_err_t platform_wifi_sta_connect(void)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;

    wifi_config_t sta_config = { 0 };
    copy_string((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), s_sta_ssid);
    copy_string((char *)sta_config.sta.password, sizeof(sta_config.sta.password), s_sta_password);
    sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_config.sta.threshold.rssi = -127;
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) return err;
    return esp_wifi_connect();
}

esp_err_t platform_wifi_sta_disconnect(void)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;
    return esp_wifi_disconnect();
}

esp_err_t platform_wifi_apply_sta_network_config(
    const platform_wifi_sta_network_config_t *network)
{
    if (!network) return ESP_ERR_INVALID_ARG;
    return apply_sta_network_config(network);
}

bool platform_wifi_sta_is_connected(void)
{
    return s_started && s_sta_connected;
}

esp_err_t platform_wifi_ap_start(void)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;
    if (s_ap_started) return ESP_OK;

    wifi_config_t ap_config = { 0 };
    copy_string((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), s_ap_ssid);
    copy_string((char *)ap_config.ap.password, sizeof(ap_config.ap.password), s_ap_password);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = s_ap_channel;
    /* Mật khẩu >= 8 ký tự → WPA2; ngược lại → mở (open) */
    ap_config.ap.authmode = strlen((char *)ap_config.ap.password) >= 8
                                ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = s_ap_max_clients;
    ap_config.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start local configuration AP: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Local configuration AP is available: %s (%s)", s_ap_ssid, s_ap_ip);
    return ESP_OK;
}

esp_err_t platform_wifi_ap_stop(void)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) return err;
    if ((mode & WIFI_MODE_AP) == 0) {
        /* AP đã dừng: trạng thái factual đã được cập nhật qua AP_STOP event */
        return ESP_OK;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Local configuration AP stopped");
    }
    return err;
}

uint8_t platform_wifi_ap_client_count(void)
{
    return s_ap_client_count;
}

bool platform_wifi_ap_is_active(void)
{
    return s_started && s_ap_started;
}

esp_err_t platform_wifi_scan(const platform_wifi_scan_config_t *config,
                             platform_wifi_scan_record_t *records,
                             uint16_t *count)
{
    if (!config || !records || !count || *count == 0) return ESP_ERR_INVALID_ARG;
    if (!s_started) return ESP_ERR_INVALID_STATE;

    /* Provider mechanics: đảm bảo mode có khả năng STA trước khi quét */
    esp_err_t mode_err = ensure_scan_capable_mode();
    if (mode_err != ESP_OK) return mode_err;

    /* Nếu capacity lớn hơn bộ đệm tĩnh, chỉ scan tối đa bộ đệm tĩnh —
     * số lượng không phải policy của platform. */
    uint16_t capacity = *count;
    if (capacity > PLATFORM_WIFI_MAX_SCAN_STATIC) capacity = PLATFORM_WIFI_MAX_SCAN_STATIC;

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = config->show_hidden,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = config->active_min_ms,
        .scan_time.active.max = config->active_max_ms,
    };
    memset(s_scan_records, 0, sizeof(s_scan_records));

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err == ESP_ERR_WIFI_STATE) return ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) return err;

    uint16_t found = 0;
    err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK) return err;
    if (found > capacity) found = capacity;
    if (found > 0) {
        uint16_t requested = found;
        err = esp_wifi_scan_get_ap_records(&requested, s_scan_records);
        if (err != ESP_OK) return err;
        found = requested;
        if (found > capacity) found = capacity;
    }

    for (uint16_t i = 0; i < found; i++) {
        copy_string(records[i].ssid, sizeof(records[i].ssid),
                    (const char *)s_scan_records[i].ssid);
        records[i].rssi = s_scan_records[i].rssi;
        records[i].auth_mode = map_auth_mode(s_scan_records[i].authmode);
    }
    *count = found;
    return ESP_OK;
}

esp_err_t platform_wifi_get_sta_status(platform_wifi_sta_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    status->rssi = -127;
    status->connected = s_started && s_sta_connected;

    if (status->connected) {
        wifi_ap_record_t access_point = { 0 };
        if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
            copy_string(status->ssid, sizeof(status->ssid), (const char *)access_point.ssid);
            status->rssi = access_point.rssi;
        }
    }

    if (s_sta_netif) {
        esp_netif_ip_info_t ip_info = { 0 };
        if (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.ip, status->ip, sizeof(status->ip));
            esp_ip4addr_ntoa(&ip_info.gw, status->gateway, sizeof(status->gateway));
        }
    }
    return ESP_OK;
}
