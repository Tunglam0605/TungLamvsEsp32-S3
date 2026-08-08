/**
 * @file    network_status_task.c
 * @brief   Triển khai task hiển thị trạng thái mạng lên phần cứng.
 *
 *          Task chạy vòng lặp 200 ms, mỗi vòng:
 *            - Đọc trạng thái STA (Wi-Fi đã nối) và AP (SoftAP cấu hình).
 *            - Cập nhật LED trạng thái AP khi trạng thái AP thay đổi.
 *            - Khi STA có cạnh lên (disconnect→connect): phát 2 bíp và
 *              khởi động bộ đếm "STA ổn định".
 *            - Khi STA ổn định đủ WIFI_STA_STABLE_BEFORE_AP_STOP_MS (30 s),
 *              AP không còn client và không có phiên portal: tự tắt AP.
 *
 *          ═══ DIAGRAM TRẠNG THÁI AP ═══
 *          ┌──────────────┬────────────────────────────────────────────┐
 *          │ Điều kiện   │ Hành động                                 │
 *          ├──────────────┼────────────────────────────────────────────┤
 *          │ AP bật/tắt  │ Cập nhật LED DO ap_status                  │
 *          │ STA ↑ edge  │ 2 bíp (2000Hz/50%/100ms) + đặt stable_since│
 *          │ +30s ổn định│ Tắt AP nếu client=0 và portal không session│
 *          └──────────────┴────────────────────────────────────────────┘
 *
 * @note    Chính sách hiển thị (bao nhiêu bíp, tần số, độ dài) thuộc về
 *          task ứng dụng này; BSP chỉ cung cấp set/off PWM ngay lập tức.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     network_status_task.h — API start
 * @see     wifi_init.c — wifi_is_connected / wifi_ap_is_active / stop AP
 * @see     callbox_io.h — kênh DO đèn trạng thái AP
 */
#include "network_status_task.h"

#include "bsp_buzzer.h"
#include "bsp_do.h"
#include "callbox_io.h"
#include "config_portal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_init.h"

static const char *TAG = "NETWORK_STATUS";
static TaskHandle_t s_task;
static volatile int8_t s_rescue_ap_beep_request;

/* STA phải ổn định 30 giây (không mất kết nối) trước khi cho phép tắt AP */
#define WIFI_STA_STABLE_BEFORE_AP_STOP_MS 30000U

static void play_sta_connected_pattern(void)
{
    /* Chính sách mẫu (pattern) thuộc về task ứng dụng này; BSP chỉ phơi bày
     * các thao tác phần cứng đặt/tắt PWM tức thời. */
    for (int i = 0; i < 2; ++i) {
        if (bsp_buzzer_set(2000, 50) != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(100));
        bsp_buzzer_off();
        if (i == 0) vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void play_sta_disconnected_pattern(void)
{
    /* A single longer tone is intentionally distinct from the two short
     * connection tones, so an operator can identify a real STA loss. */
    if (bsp_buzzer_set(1600, 50) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(650));
        bsp_buzzer_off();
    }
}

static void play_rescue_ap_enabled_pattern(void)
{
    for (int i = 0; i < 3; ++i) {
        if (bsp_buzzer_set(2000, 50) != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(100));
        bsp_buzzer_off();
        if (i < 2) vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void play_rescue_ap_disabled_pattern(void)
{
    for (int i = 0; i < 2; ++i) {
        if (bsp_buzzer_set(1600, 50) != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(450));
        bsp_buzzer_off();
        if (i == 0) vTaskDelay(pdMS_TO_TICKS(150));
    }
}

static void network_status_task(void *arg)
{
    (void)arg;
    const callbox_io_mapping_t *mapping = callbox_io_get_mapping();
    bool last_sta = false;
    bool last_ap = false;
    bool first = true;
    TickType_t sta_stable_since = 0;

    while (true) {
        if (s_rescue_ap_beep_request > 0) {
            s_rescue_ap_beep_request = 0;
            ESP_LOGI(TAG, "Rescue AP enabled; playing confirmation pattern");
            play_rescue_ap_enabled_pattern();
        } else if (s_rescue_ap_beep_request < 0) {
            s_rescue_ap_beep_request = 0;
            ESP_LOGI(TAG, "Rescue AP disabled; playing confirmation pattern");
            play_rescue_ap_disabled_pattern();
        }
        /* Đọc trạng thái mạng hiện tại từ wifi_init */
        bool sta = wifi_is_connected() != 0;
        bool ap = wifi_ap_is_active();

        /* Cập nhật LED AP_STATUS mỗi khi trạng thái AP đổi (hoặc vòng đầu) */
        if (first || ap != last_ap) {
            if (mapping) {
                esp_err_t err = bsp_do_write(mapping->ap_status, ap);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "AP status LED DO%d update failed: %s",
                             mapping->ap_status + 1, esp_err_to_name(err));
                }
            }
            ESP_LOGI(TAG, "AP status: %s", ap ? "ON" : "OFF");
        }

        /* Cạnh lên của STA: xảy ra ở lần nối đầu và sau mỗi chu kỳ mất/nối
         * lại thật sự — đây là điểm kích hoạt chính xác cho 2 bíp báo mạng */
        if (sta && !last_sta) {
            ESP_LOGI(TAG, "STA connected; playing onboard buzzer notification");
            play_sta_connected_pattern();
            sta_stable_since = xTaskGetTickCount();
        } else if (!sta && last_sta) {
            ESP_LOGW(TAG, "STA disconnected; playing onboard buzzer notification");
            play_sta_disconnected_pattern();
            sta_stable_since = 0;
        } else if (!sta) {
            sta_stable_since = 0;
        }

        /* Tắt AP tự động khi: STA ổn định đủ lâu, AP không có client nào,
         * và portal cấu hình không còn phiên đang hoạt động */
        if (sta && ap && !wifi_rescue_ap_is_enabled() && sta_stable_since != 0 &&
            (xTaskGetTickCount() - sta_stable_since) >=
                pdMS_TO_TICKS(WIFI_STA_STABLE_BEFORE_AP_STOP_MS) &&
            wifi_ap_client_count() == 0 && !config_portal_session_active()) {
            esp_err_t err = wifi_stop_config_ap();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "STA stable for %u ms; AP idle, stopping local AP",
                         WIFI_STA_STABLE_BEFORE_AP_STOP_MS);
            }
        }

        last_sta = sta;
        last_ap = ap;
        first = false;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t network_status_task_start(void)
{
    if (s_task) return ESP_OK;
    BaseType_t ret = xTaskCreate(network_status_task, "network_status", 3072,
                                 NULL, 6, &s_task);
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void network_status_notify_rescue_ap_changed(bool enabled)
{
    s_rescue_ap_beep_request = enabled ? 1 : -1;
}
