/**
 * @file    wifi_init.c
 * @brief   Triển khai khởi tạo Wi-Fi APSTA + tự chọn mạng nhớ mạnh nhất.
 *
 *          Thiết bị bật ở chế độ APSTA:
 *            - SoftAP "CALLBOX-<id>" mở ngay (để cấu hình qua điện thoại).
 *            - Task nền "wifi_select" quét kênh, chọn profile nhớ mạnh nhất
 *              rồi kết nối STA.
 *
 *          ═══ LUỒNG KẾT NỐI STA ═══
 *          ┌─────────────┐   ┌──────────────┐   ┌───────────────────────┐
 *          │ wifi_select │ → │ scan (lock)  │ → │ set_config + connect  │
 *          │ (task 5)    │   │ 40 kết quả   │   │ (sau khi chọn profile)│
 *          └─────────────┘   └──────────────┘   └───────────────────────┘
 *
 *          Sự kiện: STA_GOT_IP → s_wifi_connected=1 (bit WIFI_CONNECTED_BIT);
 *          STA_DISCONNECTED → bật lại AP (đường khôi phục) + tự retry.
 *
 *          ═══ BOUNDARY (SAU PHASE E.3) ═══
 *          Module này CHỈ biết Wi-Fi — không include bsp_eth.h, không quyết
 *          định trạng thái Ethernet. Aggregator uplink (Wi-Fi OR Ethernet)
 *          nằm ở network_link.
 *
 * @note    Các hàm wifi_scan_lock/unlock đồng bộ việc quét giữa portal
 *          và trình chọn profile nền (chỉ 1 bên quét một lúc).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.1.0
 * @date    2026
 *
 * @see     wifi_init.h — API
 * @see     config_portal.c — dùng wifi_scan_lock + status
 * @see     network_status_task.c — đọc trạng thái STA/AP
 * @see     network_link.c — aggregator uplink Wi-Fi OR Ethernet
 */
#include "wifi_init.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "WIFI_INIT";

/* Bit sự kiện đánh dấu STA đã nhận IP */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_MAX_SCAN_RESULTS 40

/* Trạng thái nội bộ của module Wi-Fi */
static EventGroupHandle_t s_wifi_event_group;
static WifiProfile_t s_profiles[MAX_WIFI_PROFILES];
static uint8_t s_profile_count;
static uint8_t s_wifi_connected;
static uint8_t s_sta_configured;
static uint8_t s_ap_started;
static volatile uint8_t s_ap_client_count;
/* Latch do ứng dụng sở hữu: giữ AP sống sau khi giữ nút Cancel 5 giây. */
static volatile bool s_rescue_ap_enabled;
static uint8_t s_scan_in_progress;
/* Bản ghi AP lớn trên ESP-IDF; giữ chúng ngoài stack của task wifi_select. */
static wifi_ap_record_t s_scan_records[WIFI_MAX_SCAN_RESULTS];
static SemaphoreHandle_t s_scan_mutex;
static volatile bool s_scan_lock_held;
static char s_ap_ssid[33] = "CALLBOX-01";
static char s_ap_password[64] = "CALLBOX-01";
static wifi_config_ap_callback_t s_ap_callback;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

/* Hàm phụ: chép chuỗi an toàn, luôn kết thúc '\0' */
static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    strncpy(dst, src ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/* Hàm phụ: phân giải chuỗi IPv4 → esp_ip4_addr_t */
static bool parse_ipv4(const char *text, esp_ip4_addr_t *address)
{
    return text && text[0] && address && esp_netif_str_to_ip4(text, address) == ESP_OK;
}

/* Cấu hình IP tĩnh cho STA (bỏ DHCP) nếu config->wifi_dhcp = false */
static esp_err_t configure_sta_ip(const Config_t *config)
{
    if (!config || config->wifi_dhcp) return ESP_OK;

    /* Kiểm tra điều kiện: cần đủ IP, netmask, gateway, DNS hợp lệ */
    if (!s_sta_netif || !parse_ipv4(config->wifi_ip, &(esp_ip4_addr_t){0}) ||
        !parse_ipv4(config->wifi_netmask, &(esp_ip4_addr_t){0}) ||
        !parse_ipv4(config->wifi_gateway, &(esp_ip4_addr_t){0}) ||
        !parse_ipv4(config->wifi_dns, &(esp_ip4_addr_t){0})) {
        ESP_LOGE(TAG, "Static IP mode requires valid IP, netmask, gateway and DNS");
        return ESP_ERR_INVALID_ARG;
    }

    /* Dịch chuỗi sang cấu trúc ip_info */
    esp_netif_ip_info_t ip_info = { 0 };
    esp_err_t err = esp_netif_str_to_ip4(config->wifi_ip, &ip_info.ip);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(config->wifi_netmask, &ip_info.netmask);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(config->wifi_gateway, &ip_info.gw);
    if (err != ESP_OK) return err;

    /* Dừng DHCP client rồi gán IP tĩnh cho netif STA */
    err = esp_netif_dhcpc_stop(s_sta_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    err = esp_netif_set_ip_info(s_sta_netif, &ip_info);
    if (err != ESP_OK) return err;

    /* Gán DNS chính */
    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    err = esp_netif_str_to_ip4(config->wifi_dns, &dns.ip.u_addr.ip4);
    if (err == ESP_OK) err = esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi IP mode: static %s (gateway=%s)", config->wifi_ip, config->wifi_gateway);
    }
    return err;
}

/* Cấu hình mạng cho SoftAP: IP cố định + DHCP server (vùng 192.168.65.x) */
static esp_err_t configure_ap_netif(void)
{
    if (!s_ap_netif) return ESP_ERR_INVALID_STATE;

    /* Gán IP/địa chỉ mạng cho AP netif */
    esp_netif_ip_info_t ip_info = { 0 };
    esp_err_t err = esp_netif_str_to_ip4(CALLBOX_AP_IP_ADDR, &ip_info.ip);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(CALLBOX_AP_IP_ADDR, &ip_info.gw);
    if (err == ESP_OK) err = esp_netif_str_to_ip4(CALLBOX_AP_NETMASK, &ip_info.netmask);
    if (err != ESP_OK) return err;

    /* Dừng DHCP server → gán IP → bật lại DHCP (điện thoại nhận IP tự động) */
    err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
    if (err != ESP_OK) return err;
    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return err;

    ESP_LOGI(TAG, "Configuration AP network: %s/24, DHCP server enabled", CALLBOX_AP_IP_ADDR);
    return ESP_OK;
}

/* Bật SoftAP cấu hình (nếu chưa bật): SSID, mật khẩu, WPA2 */
static esp_err_t wifi_start_config_ap(void)
{
    if (s_ap_started) return ESP_OK;

    wifi_config_t ap_config = { 0 };
    copy_string((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), s_ap_ssid);
    copy_string((char *)ap_config.ap.password, sizeof(ap_config.ap.password), s_ap_password);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = 1;
    /* Mật khẩu >= 8 ký tự → WPA2; ngược lại → mở (open) */
    ap_config.ap.authmode = strlen((char *)ap_config.ap.password) >= 8
                                ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    ap_config.ap.pmf_cfg.required = false;

    /* Chuyển sang APSTA (nếu chưa) và áp cấu hình AP */
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start local configuration AP: %s", esp_err_to_name(err));
        return err;
    }
    s_ap_started = 1;
    ESP_LOGI(TAG, "Local configuration AP is available: %s (%s)", s_ap_ssid, CALLBOX_AP_IP_ADDR);
    return ESP_OK;
}

/* Chọn profile nhớ có SSID xuất hiện trong kết quả quét với RSSI mạnh nhất */
static int find_best_profile(const wifi_ap_record_t *records, uint16_t count)
{
    int best = -1;
    int best_rssi = -127;
    for (uint8_t p = 0; p < s_profile_count; p++) {
        for (uint16_t i = 0; i < count; i++) {
            if (strcmp((const char *)records[i].ssid, s_profiles[p].ssid) == 0 &&
                records[i].rssi > best_rssi) {
                best = p;
                best_rssi = records[i].rssi;
            }
        }
    }
    return best;
}

/* Task nền: quét + chọn mạng nhớ mạnh nhất + kết nối STA (vòng lặp vô hạn) */
static void connect_to_best_known_network(void *arg)
{
    (void)arg;
    if (s_profile_count == 0) {
        ESP_LOGW(TAG, "No remembered Wi-Fi profiles; waiting for local AP fallback");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
    if (s_wifi_connected) {
        /* Đã kết nối: chờ 10 s rồi kiểm tra lại */
        vTaskDelay(pdMS_TO_TICKS(10000));
        continue;
    }
    if (s_scan_in_progress || s_ap_client_count > 0) {
        /* Đang quét hoặc có client AP → không phá mạng của người cấu hình */
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
    }
    /* Xin quyền quét duy nhất (portal có thể đang quét) */
    if (!wifi_scan_lock(1000)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
    }
    s_scan_in_progress = 1;

    /* Cấu hình quét: tất cả kênh, quét chủ động, hiện mạng ẩn */
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    memset(s_scan_records, 0, sizeof(s_scan_records));
    uint16_t count = 0;
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_num(&count);
    if (err == ESP_OK && count > WIFI_MAX_SCAN_RESULTS) count = WIFI_MAX_SCAN_RESULTS;
    if (err == ESP_OK && count > 0) {
        uint16_t requested = count;
        err = esp_wifi_scan_get_ap_records(&requested, s_scan_records);
        count = requested;
    }

    if (err == ESP_ERR_WIFI_STATE) {
        /* STA đang hoàn tất kết nối trước đó — không gọi set_config/connect
         * trong trạng thái này; chờ chu kỳ quét sau. */
        s_scan_in_progress = 0;
        wifi_scan_unlock();
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
    }

    /* Chọn profile: ưu tiên mạng nhớ thấy trong kết quả quét (mạnh nhất);
     * nếu không thấy → dùng profile đầu (mặc định) */
    int selected = (err == ESP_OK) ? find_best_profile(s_scan_records, count) : -1;
    if (selected < 0) selected = 0;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed (%s); trying remembered profile %d", esp_err_to_name(err), selected);
    } else {
        ESP_LOGI(TAG, "Selected remembered Wi-Fi profile %d/%u: %s%s",
                 selected + 1, s_profile_count, s_profiles[selected].ssid,
                 find_best_profile(s_scan_records, count) >= 0 ? " (strongest visible)" : " (default)");
    }

    /* Cấu hình STA theo profile chọn rồi gọi esp_wifi_connect() */
    wifi_config_t sta_config = { 0 };
    copy_string((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), s_profiles[selected].ssid);
    copy_string((char *)sta_config.sta.password, sizeof(sta_config.sta.password), s_profiles[selected].password);
    sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_config.sta.threshold.rssi = -127;
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err == ESP_OK) {
        s_sta_configured = 1;
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect start failed: %s", esp_err_to_name(err));
    }
    s_scan_in_progress = 0;
    wifi_scan_unlock();
    vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* Sự kiện Wi-Fi: cập nhật cờ trạng thái, bật lại AP khi mất STA, retry.
 * Private — chỉ được đăng ký nội bộ trong module này. */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                       int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                s_wifi_connected = 0;
                ESP_LOGI(TAG, "Wi-Fi STA started; selecting strongest remembered network");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_wifi_connected = 0;
                /* Giữ AP cấu hình luôn sẵn sàng ngay khi mất STA — đây là
                 * đường khôi phục của điện thoại, không đợi timer dự phòng. */
                (void)wifi_start_config_ap();
                if (s_sta_configured && !s_scan_lock_held) {
                    ESP_LOGW(TAG, "Wi-Fi disconnected, retrying current profile...");
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_AP_START:
                s_ap_started = 1;
                ESP_LOGI(TAG, "Local configuration AP started: %s", s_ap_ssid);
                if (s_ap_callback) s_ap_callback();
                break;
            case WIFI_EVENT_AP_STOP:
                s_ap_started = 0;
                s_ap_client_count = 0;
                ESP_LOGW(TAG, "Local configuration AP stopped");
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                if (s_ap_client_count < 255) s_ap_client_count++;
                ESP_LOGI(TAG, "Configuration client connected (count=%u)", s_ap_client_count);
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                if (s_ap_client_count > 0) s_ap_client_count--;
                ESP_LOGI(TAG, "Configuration client disconnected (count=%u)", s_ap_client_count);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* STA nhận IP → đánh dấu kết nối thành công + set bit cho ai chờ */
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_wifi_connected = 1;
        ESP_LOGI(TAG, "Wi-Fi connected, got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta_profiles(const Config_t *config,
                                 const char *ap_ssid, const char *ap_password)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    /* Nạp profile từ config; nếu trống → dùng wifi_ssid/pass làm profile đầu */
    s_profile_count = config->wifi_profile_count;
    if (s_profile_count > MAX_WIFI_PROFILES) s_profile_count = MAX_WIFI_PROFILES;
    for (uint8_t i = 0; i < s_profile_count; i++) s_profiles[i] = config->wifi_profiles[i];
    if (s_profile_count == 0 && config->wifi_ssid[0]) {
        copy_string(s_profiles[0].ssid, sizeof(s_profiles[0].ssid), config->wifi_ssid);
        copy_string(s_profiles[0].password, sizeof(s_profiles[0].password), config->wifi_pass);
        s_profile_count = 1;
    }
    copy_string(s_ap_ssid, sizeof(s_ap_ssid), ap_ssid && ap_ssid[0] ? ap_ssid : "CALLBOX-01");
    /* Mật khẩu cố tình giống SSID AP trừ khi người gọi cung cấp giá trị
     * khác. Điều này hợp lệ WPA2 và đơn giản cho việc khôi phục tại hiện
     * trường. */
    copy_string(s_ap_password, sizeof(s_ap_password),
                ap_password && ap_password[0] ? ap_password : s_ap_ssid);

    /* Tạo event group + mutex quét (chung giữa portal và task nền) */
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) return ESP_ERR_NO_MEM;
    if (!s_scan_mutex) {
        s_scan_mutex = xSemaphoreCreateMutex();
        if (!s_scan_mutex) return ESP_ERR_NO_MEM;
    }

    /* Khởi tạo netif + event loop (chấp nhận đã init trước) */
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    /* Tạo 2 netif: STA + AP */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;

    /* Cấu hình IP: AP luôn cố định; STA theo DHCP hoặc IP tĩnh */
    ret = configure_ap_netif();
    if (ret != ESP_OK) return ret;
    ret = configure_sta_ip(config);
    if (ret != ESP_OK) return ret;

    /* Khởi tạo driver Wi-Fi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) return ret;

    /* Đăng ký sự kiện: toàn bộ WIFI_EVENT + IP_EVENT_STA_GOT_IP */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) return ret;
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) return ret;

    /* Reset trạng thái và bật APSTA: AP cấu hình mở ngay, STA chọn mạng song song */
    s_sta_configured = 0;
    s_ap_started = 0;
    s_ap_client_count = 0;
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret == ESP_OK) (void)wifi_start_config_ap();
    if (ret == ESP_OK) ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STOPPED) return ret;

    /* Tạo task chọn mạng nền */
    xTaskCreate(connect_to_best_known_network, "wifi_select", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Wi-Fi started in APSTA mode; local AP is available immediately");
    return ESP_OK;
}

void wifi_set_config_ap_callback(wifi_config_ap_callback_t callback)
{
    /* Lưu callback (được gọi khi AP_START) — thường là mở portal cấu hình */
    s_ap_callback = callback;
}

esp_err_t wifi_init_apsta(const char *ssid, const char *password,
                          const char *ap_ssid, const char *ap_password)
{
    /* Helper tương thích cũ: 1 profile + bật AP ngay */
    Config_t config = { 0 };
    copy_string(config.wifi_ssid, sizeof(config.wifi_ssid), ssid);
    copy_string(config.wifi_pass, sizeof(config.wifi_pass), password);
    config.wifi_profile_count = 1;
    copy_string(config.wifi_profiles[0].ssid, sizeof(config.wifi_profiles[0].ssid), ssid);
    copy_string(config.wifi_profiles[0].password, sizeof(config.wifi_profiles[0].password), password);
    esp_err_t ret = wifi_init_sta_profiles(&config, ap_ssid, ap_password);
    if (ret == ESP_OK) wifi_start_config_ap();
    return ret;
}

esp_err_t wifi_apply_config(const Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    /* Nạp lại profile; nếu trống → tạo profile từ wifi_ssid/pass */
    uint8_t count = config->wifi_profile_count;
    if (count > MAX_WIFI_PROFILES) count = MAX_WIFI_PROFILES;
    for (uint8_t i = 0; i < count; ++i) s_profiles[i] = config->wifi_profiles[i];
    if (count == 0 && config->wifi_ssid[0]) {
        copy_string(s_profiles[0].ssid, sizeof(s_profiles[0].ssid), config->wifi_ssid);
        copy_string(s_profiles[0].password, sizeof(s_profiles[0].password), config->wifi_pass);
        count = 1;
    }
    s_profile_count = count;

    /* Áp lại cấu hình IP (DHCP/tĩnh) nếu thay đổi */
    esp_err_t err = configure_sta_ip(config);
    if (err != ESP_OK) return err;

    if (!s_profile_count) {
        /* Xóa profile cuối phải có hiệu lực ngay — nếu không, STA có thể
         * vẫn bám mạng cũ (không còn trong NVS) cho đến khi reboot. */
        memset(s_profiles, 0, sizeof(s_profiles));
        s_sta_configured = 0;
        s_wifi_connected = 0;
        (void)esp_wifi_disconnect();
        ESP_LOGI(TAG, "Cleared all remembered Wi-Fi profiles");
        return ESP_OK;
    }

    /* Cấu hình STA theo profile đầu (ưu tiên) và kết nối lại ngay */
    wifi_config_t sta = { 0 };
    copy_string((char *)sta.sta.ssid, sizeof(sta.sta.ssid), s_profiles[0].ssid);
    copy_string((char *)sta.sta.password, sizeof(sta.sta.password), s_profiles[0].password);
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta.sta.threshold.rssi = -127;
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
    err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err == ESP_OK) {
        s_sta_configured = 1;
        s_wifi_connected = 0;
        err = esp_wifi_connect();
    }
    ESP_LOGI(TAG, "Applied Wi-Fi configuration without reboot: %s (%s)",
             s_profiles[0].ssid, esp_err_to_name(err));
    return err;
}

void wifi_init_sta(const char *ssid, const char *password)
{
    /* Helper tương thích cũ: 1 profile STA + AP có SSID/mật khẩu trùng nhau. */
    esp_err_t ret = wifi_init_apsta(ssid, password, "CALLBOX-01", "CALLBOX-01");
    if (ret != ESP_OK) ESP_LOGE(TAG, "WiFi initialization failed: %s", esp_err_to_name(ret));
}

uint8_t wifi_is_connected(void)
{
    return s_wifi_connected;
}

void wifi_get_sta_status(wifi_sta_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    status->rssi = -127;
    status->connected = s_wifi_connected != 0;

    /* Lấy thông tin AP đang kết nối (SSID + RSSI) nếu có */
    wifi_ap_record_t access_point = { 0 };
    if (status->connected && esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
        copy_string(status->ssid, sizeof(status->ssid), (const char *)access_point.ssid);
        status->rssi = access_point.rssi;
    }

    /* Lấy IP/gateway từ netif STA */
    esp_netif_ip_info_t ip_info = { 0 };
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
        esp_ip4addr_ntoa(&ip_info.ip, status->ip, sizeof(status->ip));
        esp_ip4addr_ntoa(&ip_info.gw, status->gateway, sizeof(status->gateway));
    }
}

bool wifi_ap_is_active(void)
{
    return s_ap_started != 0;
}

bool wifi_rescue_ap_is_enabled(void)
{
    return s_rescue_ap_enabled;
}

uint8_t wifi_ap_client_count(void)
{
    return s_ap_client_count;
}

esp_err_t wifi_stop_config_ap(void)
{
    if (!s_ap_started) return ESP_OK;
    /* Không tắt AP khi còn client hoặc đang quét */
    if (s_ap_client_count > 0 || s_scan_lock_held) return ESP_ERR_INVALID_STATE;

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) return err;
    if ((mode & WIFI_MODE_AP) == 0) {
        s_ap_started = 0;
        return ESP_OK;
    }

    /* Chuyển STA-only: tắt hẳn phần AP */
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Local configuration AP stopped: STA is stable and AP is idle");
    }
    return err;
}

esp_err_t wifi_toggle_rescue_ap(bool *enabled)
{
    const bool next = !s_rescue_ap_enabled;

    if (next) {
        const esp_err_t err = wifi_start_config_ap();
        if (err != ESP_OK) return err;
        s_rescue_ap_enabled = true;
        if (enabled) *enabled = true;
        ESP_LOGW(TAG, "Rescue AP enabled by Cancel long press");
        return ESP_OK;
    }

    /* Xóa latch rõ ràng trước. Nếu vẫn còn client AP kết nối, chính sách
     * nhàn rỗi bình thường sẽ đóng AP khi an toàn. */
    s_rescue_ap_enabled = false;
    if (enabled) *enabled = false;
    if (s_wifi_connected) {
        const esp_err_t err = wifi_stop_config_ap();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    }
    ESP_LOGI(TAG, "Rescue AP latch disabled by Cancel long press");
    return ESP_OK;
}

bool wifi_scan_lock(uint32_t timeout_ms)
{
    if (!s_scan_mutex) return true;
    /* Chờ mutex tối đa timeout_ms; có được thì đánh dấu đang giữ lock */
    if (xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    s_scan_lock_held = true;
    return true;
}

void wifi_scan_unlock(void)
{
    s_scan_lock_held = false;
    if (s_scan_mutex) xSemaphoreGive(s_scan_mutex);
}
