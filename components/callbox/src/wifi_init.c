/**
 * @file    wifi_init.c
 * @brief   Product Wi-Fi policy: APSTA + tự chọn mạng nhớ mạnh nhất.
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
 *          ═══ BOUNDARY (SAU PHASE E.3.1) ═══
 *          Module này CHỈ giữ policy của product:
 *            - Config_t mapping → platform_wifi_* config
 *            - WifiProfile_t + find_best_profile (chọn mạng mạnh nhất)
 *            - Scan coordination (wifi_scan_lock/unlock với portal)
 *            - Rescue AP latch + AP recovery khi STA mất
 *            - Task wifi_select + thời điểm retry
 *            - Callback mở config portal khi AP_STARTED
 *          MỌI provider mechanics của ESP-IDF (esp_wifi_*, esp_netif_*,
 *          WIFI_EVENT / IP_EVENT, event handler) nằm ở platform_wifi.
 *
 *          ═══ UPLINK ═══
 *          Không include bsp_eth.h — aggregator uplink (Wi-Fi OR Ethernet)
 *          nằm ở network_link. Module này CHỈ theo dõi trạng thái Wi-Fi.
 *
 * @note    Các hàm wifi_scan_lock/unlock đồng bộ việc quét giữa portal
 *          và trình chọn profile nền (chỉ 1 bên quét một lúc).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.2.0
 * @date    2026
 *
 * @see     wifi_init.h — API
 * @see     config_portal.c — dùng wifi_scan_lock + status
 * @see     network_status_task.c — đọc trạng thái STA/AP
 * @see     network_link.c — aggregator uplink Wi-Fi OR Ethernet
 * @see     platform_wifi.h — provider ESP-IDF Wi-Fi
 */
#include "wifi_init.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "platform_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "WIFI_INIT";

/* Bit sự kiện đánh dấu STA đã nhận IP */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_MAX_SCAN_RESULTS 40

/* Trạng thái nội bộ của module Wi-Fi (policy của product) */
static EventGroupHandle_t s_wifi_event_group;
static WifiProfile_t s_profiles[MAX_WIFI_PROFILES];
static uint8_t s_profile_count;
static uint8_t s_sta_configured;
/* Latch do ứng dụng sở hữu: giữ AP sống sau khi giữ nút Cancel 5 giây. */
static volatile bool s_rescue_ap_enabled;
static uint8_t s_scan_in_progress;
/* Bản ghi quét generic của platform (capacity 40 = policy của product). */
static platform_wifi_scan_record_t s_scan_records[WIFI_MAX_SCAN_RESULTS];
static SemaphoreHandle_t s_scan_mutex;
static volatile bool s_scan_lock_held;
static char s_ap_ssid[33] = "CALLBOX-01";
static char s_ap_password[64] = "CALLBOX-01";
static wifi_config_ap_callback_t s_ap_callback;
/* Thông báo thay đổi Rescue AP — module này không biết network_status_task,
 * BSP buzzer hay GPIO46; composition root (app_main) đăng ký triển khai. */
static wifi_rescue_ap_changed_callback_t s_rescue_callback;

/* Hàm phụ: chép chuỗi an toàn, luôn kết thúc '\0' */
static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    strncpy(dst, src ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/* Cấu hình IP cho STA (DHCP/tĩnh) từ Config_t → platform_wifi config */
static esp_err_t configure_sta_ip(const Config_t *config)
{
    if (!config || config->wifi_dhcp) return ESP_OK;

    const platform_wifi_sta_network_config_t network = {
        .dhcp = false,
        .ip = config->wifi_ip,
        .netmask = config->wifi_netmask,
        .gateway = config->wifi_gateway,
        .dns = config->wifi_dns,
    };
    return platform_wifi_apply_sta_network_config(&network);
}

/* Chọn profile nhớ có SSID xuất hiện trong kết quả quét với RSSI mạnh nhất */
static int find_best_profile(const platform_wifi_scan_record_t *records, uint16_t count)
{
    int best = -1;
    int best_rssi = -127;
    for (uint8_t p = 0; p < s_profile_count; p++) {
        for (uint16_t i = 0; i < count; i++) {
            if (strcmp(records[i].ssid, s_profiles[p].ssid) == 0 &&
                records[i].rssi > best_rssi) {
                best = p;
                best_rssi = records[i].rssi;
            }
        }
    }
    return best;
}

/* Bật SoftAP cấu hình qua platform (nếu chưa bật) */
static esp_err_t wifi_start_config_ap(void)
{
    return platform_wifi_ap_start();
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
    if (platform_wifi_sta_is_connected()) {
        /* Đã kết nối: chờ 10 s rồi kiểm tra lại */
        vTaskDelay(pdMS_TO_TICKS(10000));
        continue;
    }
    if (s_scan_in_progress || platform_wifi_ap_client_count() > 0) {
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
    const platform_wifi_scan_config_t scan_config = {
        .show_hidden = true,
        .active_min_ms = 100,
        .active_max_ms = 300,
    };
    memset(s_scan_records, 0, sizeof(s_scan_records));
    uint16_t count = WIFI_MAX_SCAN_RESULTS;
    esp_err_t err = platform_wifi_scan(&scan_config, s_scan_records, &count);

    if (err == ESP_ERR_INVALID_STATE) {
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

    /* Cấu hình STA theo profile chọn rồi kết nối */
    esp_err_t connect_err = platform_wifi_sta_set_credentials(
        s_profiles[selected].ssid, s_profiles[selected].password);
    if (connect_err == ESP_OK) {
        s_sta_configured = 1;
        connect_err = platform_wifi_sta_connect();
    }
    if (connect_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect start failed: %s", esp_err_to_name(connect_err));
    }
    s_scan_in_progress = 0;
    wifi_scan_unlock();
    vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* Policy sự kiện: giữ nguyên hành vi cũ; trạng thái factual nằm ở platform. */
static void on_platform_wifi_event(platform_wifi_event_t event, void *context)
{
    (void)context;
    switch (event) {
    case PLATFORM_WIFI_EVENT_STA_STARTED:
        ESP_LOGI(TAG, "Wi-Fi STA started; selecting strongest remembered network");
        break;
    case PLATFORM_WIFI_EVENT_STA_DISCONNECTED:
        /* Giữ AP cấu hình luôn sẵn sàng ngay khi mất STA — đây là
         * đường khôi phục của điện thoại, không đợi timer dự phòng. */
        (void)wifi_start_config_ap();
        if (s_sta_configured && !s_scan_lock_held) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, retrying current profile...");
            (void)platform_wifi_sta_connect();
        }
        break;
    case PLATFORM_WIFI_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "Wi-Fi connected, got IP");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case PLATFORM_WIFI_EVENT_AP_STARTED:
        ESP_LOGI(TAG, "Local configuration AP started: %s", s_ap_ssid);
        if (s_ap_callback) s_ap_callback();
        break;
    case PLATFORM_WIFI_EVENT_AP_STOPPED:
        ESP_LOGW(TAG, "Local configuration AP stopped");
        break;
    case PLATFORM_WIFI_EVENT_AP_CLIENT_CONNECTED:
        ESP_LOGI(TAG, "Configuration client connected (count=%u)",
                 platform_wifi_ap_client_count());
        break;
    case PLATFORM_WIFI_EVENT_AP_CLIENT_DISCONNECTED:
        ESP_LOGI(TAG, "Configuration client disconnected (count=%u)",
                 platform_wifi_ap_client_count());
        break;
    default:
        break;
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

    /* Map Config_t → platform config (IP tĩnh/DHCP) */
    const platform_wifi_sta_network_config_t sta_network = {
        .dhcp = config->wifi_dhcp,
        .ip = config->wifi_ip,
        .netmask = config->wifi_netmask,
        .gateway = config->wifi_gateway,
        .dns = config->wifi_dns,
    };
    const platform_wifi_ap_config_t ap_config = {
        .ssid = s_ap_ssid,
        .password = s_ap_password,
        .ip = CALLBOX_AP_IP_ADDR,
        .netmask = CALLBOX_AP_NETMASK,
        .channel = 1,
        .max_clients = 4,
    };

    /* Khởi tạo provider + bật APSTA (netif/event loop/event handler trong
     * platform_wifi; giữ thứ tự gọi sớm cho W5500 BSP) */
    esp_err_t ret = platform_wifi_start_apsta(&sta_network, &ap_config,
                                              on_platform_wifi_event, NULL);
    if (ret != ESP_OK) return ret;

    /* Reset trạng thái và bật APSTA: AP cấu hình mở ngay, STA chọn mạng song song */
    s_sta_configured = 0;
    (void)wifi_start_config_ap();

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

void wifi_set_rescue_ap_changed_callback(wifi_rescue_ap_changed_callback_t callback)
{
    /* Một con trỏ callback duy nhất, không registry, không event bus. */
    s_rescue_callback = callback;
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
        (void)platform_wifi_sta_disconnect();
        ESP_LOGI(TAG, "Cleared all remembered Wi-Fi profiles");
        return ESP_OK;
    }

    /* Cấu hình STA theo profile đầu (ưu tiên) và kết nối lại ngay */
    err = platform_wifi_sta_set_credentials(s_profiles[0].ssid, s_profiles[0].password);
    if (err == ESP_OK) {
        s_sta_configured = 1;
        err = platform_wifi_sta_connect();
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
    return platform_wifi_sta_is_connected() ? 1U : 0U;
}

void wifi_get_sta_status(wifi_sta_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));

    /* Trạng thái factual + thông tin AP/IP lấy từ provider */
    platform_wifi_sta_status_t provider = { 0 };
    if (platform_wifi_get_sta_status(&provider) != ESP_OK) return;
    status->connected = provider.connected;
    copy_string(status->ssid, sizeof(status->ssid), provider.ssid);
    status->rssi = provider.rssi;
    copy_string(status->ip, sizeof(status->ip), provider.ip);
    copy_string(status->gateway, sizeof(status->gateway), provider.gateway);
}

bool wifi_ap_is_active(void)
{
    return platform_wifi_ap_is_active();
}

bool wifi_rescue_ap_is_enabled(void)
{
    return s_rescue_ap_enabled;
}

uint8_t wifi_ap_client_count(void)
{
    return platform_wifi_ap_client_count();
}

esp_err_t wifi_stop_config_ap(void)
{
    if (!platform_wifi_ap_is_active()) return ESP_OK;
    /* Không tắt AP khi còn client hoặc đang quét */
    if (platform_wifi_ap_client_count() > 0 || s_scan_lock_held) return ESP_ERR_INVALID_STATE;

    return platform_wifi_ap_stop();
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
        if (s_rescue_callback) s_rescue_callback(true);
        return ESP_OK;
    }

    /* Xóa latch rõ ràng trước. Nếu vẫn còn client AP kết nối, chính sách
     * nhàn rỗi bình thường sẽ đóng AP khi an toàn. */
    s_rescue_ap_enabled = false;
    if (enabled) *enabled = false;
    if (platform_wifi_sta_is_connected()) {
        const esp_err_t err = wifi_stop_config_ap();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    }
    ESP_LOGI(TAG, "Rescue AP latch disabled by Cancel long press");
    if (s_rescue_callback) s_rescue_callback(false);
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
