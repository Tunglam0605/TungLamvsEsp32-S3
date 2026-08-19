/**
 * @file led_control.c
 * @brief Các lệnh gốc phần cứng đầu ra, chỉ được sử dụng bởi Output Renderer.
 */
#include "led_control.h"

#include "bsp_do.h"
#include "callbox_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "health_monitor.h"

static const char *TAG = "LED_CONTROL";

/* Queue lệnh buzzer nghiệp vụ — thuộc sở hữu riêng của led_control, không
 * còn là biến toàn cục của app_main. Độ sâu giữ nguyên 10 như baseline. */
static QueueHandle_t s_buzzer_queue = NULL;
static TaskHandle_t s_buzzer_task = NULL;

#define BUZZER_QUEUE_DEPTH 10

static LEDState_t s_button_state[3] = { LED_OFF, LED_OFF, LED_OFF };
static bool s_button_phase[3];
static TickType_t s_button_last_toggle[3];
static uint8_t s_button_flash_remaining[3];
static int s_tower_color;
static LEDState_t s_tower_state = LED_OFF;
static bool s_tower_phase;
static TickType_t s_tower_last_toggle;
static uint8_t s_tower_double_step;
static uint8_t s_tick_error_streak;
static TickType_t s_tick_last_error_log;

#define BUTTON_BLINK_SLOW_MS 500U
#define BUTTON_BLINK_FAST_MS 150U
#define TOWER_BLINK_SLOW_MS  500U
#define TOWER_BLINK_FAST_MS  250U
#define TOWER_DOUBLE_ON_MS    180U
#define TOWER_DOUBLE_GAP_MS   180U
#define TOWER_DOUBLE_PAUSE_MS 1000U

static esp_err_t do_write(bsp_do_channel_t channel, bool active)
{
    return bsp_do_write(channel, active);
}

static void note_tick_result(esp_err_t result)
{
    if (result == ESP_OK) {
        if (s_tick_error_streak != 0U) {
            ESP_LOGI(TAG, "I2C output writes recovered after %u failed tick(s)",
                     s_tick_error_streak);
        }
        s_tick_error_streak = 0U;
        return;
    }

    if (s_tick_error_streak < UINT8_MAX) s_tick_error_streak++;
    const TickType_t now = xTaskGetTickCount();
    if (s_tick_error_streak == 1U ||
        now - s_tick_last_error_log >= pdMS_TO_TICKS(5000U)) {
        ESP_LOGW(TAG, "I2C output tick failed (%u consecutive): %s",
                 s_tick_error_streak, esp_err_to_name(result));
        s_tick_last_error_log = now;
    }
}

static bsp_do_channel_t button_channel(int button_id)
{
    const callbox_io_mapping_t *mapping = callbox_io_get_mapping();
    return button_id == 1 ? mapping->led_task1 :
           button_id == 2 ? mapping->led_task2 : mapping->led_cancel;
}

static bsp_do_channel_t tower_channel(int color)
{
    const callbox_io_mapping_t *mapping = callbox_io_get_mapping();
    return color == 1 ? mapping->tower_red :
           color == 2 ? mapping->tower_yellow : mapping->tower_green;
}

static void buzzer_task(void *arg)
{
    (void)arg;
    BuzzerCmd_t command;
    while (xQueueReceive(s_buzzer_queue, &command, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < command.beep_count; ++i) {
            const bsp_do_channel_t buzzer = callbox_io_get_mapping()->buzzer;
            const esp_err_t on_err = do_write(buzzer, true);
            if (on_err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(command.duration_ms));
            }

            /* OFF là trạng thái an toàn. Nếu nhiễu I2C xảy ra đúng lúc kết thúc
             * tiếng bíp, retry có backoff trong task riêng cho tới khi TCA xác
             * nhận OFF. Việc này không khóa Mission/Renderer và ngăn còi DO1 bị
             * giữ ON vô hạn sau một lỗi bus thoáng qua. */
            uint32_t retry_ms = 20U;
            uint32_t failures = 0U;
            TickType_t last_log = 0;
            esp_err_t off_err;
            while ((off_err = do_write(buzzer, false)) != ESP_OK) {
                failures++;
                const TickType_t now = xTaskGetTickCount();
                if (failures == 1U || now - last_log >= pdMS_TO_TICKS(5000U)) {
                    ESP_LOGE(TAG, "Buzzer safe-OFF failed (%lu attempts): %s",
                             (unsigned long)failures, esp_err_to_name(off_err));
                    last_log = now;
                }
                vTaskDelay(pdMS_TO_TICKS(retry_ms));
                if (retry_ms < 1000U) {
                    retry_ms = retry_ms * 2U > 1000U ? 1000U : retry_ms * 2U;
                }
                /* Không để worker kẹt vô hạn nếu bus/expander hỏng
                 * vĩnh viễn. Sau 8 lần (xấp xỉ 3,3 giây backoff),
                 * controlled restart sẽ tạo lại bus và đưa DO về safe. */
                if (failures >= 8U) {
                    health_monitor_force_restart("buzzer_safe_off_failed");
                }
            }
            if (failures != 0U) {
                ESP_LOGW(TAG, "Buzzer safe-OFF recovered after %lu attempt(s)",
                         (unsigned long)failures);
            }
            if (i + 1 < command.beep_count) vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t led_control_prepare(void)
{
    /* Idempotent: nếu queue đã tồn tại (đã gọi lần đầu) thì không tạo lại.
     * Chỉ tạo queue lệnh buzzer nghiệp vụ — không tạo task, không chạm BSP,
     * không ghi phần cứng. Thất bại cấp phát trả ESP_ERR_NO_MEM để app_main
     * dừng boot tại cùng điểm chết như baseline (trước bsp_board_init). */
    if (s_buzzer_queue) return ESP_OK;
    s_buzzer_queue = xQueueCreate(BUZZER_QUEUE_DEPTH, sizeof(BuzzerCmd_t));
    if (!s_buzzer_queue) {
        ESP_LOGE(TAG, "Failed to create buzzer queue");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t led_control_init(void)
{
    if (s_buzzer_task) return ESP_OK;
    if (!s_buzzer_queue) {
        ESP_LOGE(TAG, "Buzzer queue unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 6,
                    &s_buzzer_task) != pdPASS) {
        s_buzzer_task = NULL;
        ESP_LOGE(TAG, "Buzzer task unavailable");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Output primitives ready via BSP");
    return ESP_OK;
}

esp_err_t set_button_led(int button_id, LEDState_t state)
{
    if (button_id < 1 || button_id > 3) return ESP_ERR_INVALID_ARG;
    const int index = button_id - 1;
    const bool initial_phase = state != LED_OFF;
    const esp_err_t err = do_write(button_channel(button_id), initial_phase);
    if (err != ESP_OK) return err;

    /* Chỉ commit bộ tạo pattern sau khi hardware write thành công. Nếu I2C lỗi,
     * renderer sẽ retry mà không làm lệch phase/cache phần mềm. */
    s_button_state[index] = state;
    s_button_phase[index] = initial_phase;
    s_button_flash_remaining[index] = state == LED_FLASH_2 ? 2U :
                                      state == LED_FLASH_3 ? 3U : 0U;
    s_button_last_toggle[index] = xTaskGetTickCount();
    return ESP_OK;
}

esp_err_t led_control_tick(void)
{
    esp_err_t first_error = ESP_OK;
    const TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < 3; ++i) {
        const uint32_t interval_ms = (s_button_state[i] == LED_BLINK_FAST ||
                                      s_button_state[i] == LED_FLASH_2 ||
                                      s_button_state[i] == LED_FLASH_3) ? BUTTON_BLINK_FAST_MS :
                                     s_button_state[i] == LED_BLINK_SLOW ? BUTTON_BLINK_SLOW_MS : 0U;
        if (!interval_ms || now - s_button_last_toggle[i] < pdMS_TO_TICKS(interval_ms)) continue;
        if ((s_button_state[i] == LED_FLASH_2 || s_button_state[i] == LED_FLASH_3) &&
            s_button_phase[i]) {
            const bool final_flash = s_button_flash_remaining[i] <= 1U;
            const esp_err_t err = do_write(button_channel(i + 1), false);
            if (err != ESP_OK) {
                if (first_error == ESP_OK) first_error = err;
                continue;
            }
            if (s_button_flash_remaining[i] != 0U) s_button_flash_remaining[i]--;
            s_button_phase[i] = false;
            s_button_last_toggle[i] = now;
            if (final_flash) s_button_state[i] = LED_OFF;
            continue;
        }
        const bool next_phase = !s_button_phase[i];
        const esp_err_t err = do_write(button_channel(i + 1), next_phase);
        if (err != ESP_OK) {
            if (first_error == ESP_OK) first_error = err;
            continue;
        }
        s_button_phase[i] = next_phase;
        s_button_last_toggle[i] = now;
    }

    uint32_t tower_interval_ms = s_tower_state == LED_BLINK_FAST ?
                                     TOWER_BLINK_FAST_MS :
                                 s_tower_state == LED_BLINK_SLOW ?
                                     TOWER_BLINK_SLOW_MS : 0U;
    if (s_tower_state == LED_BLINK_DOUBLE) {
        tower_interval_ms = s_tower_double_step == 3U ? TOWER_DOUBLE_PAUSE_MS :
                            s_tower_double_step == 1U ? TOWER_DOUBLE_GAP_MS :
                                                         TOWER_DOUBLE_ON_MS;
    }
    if (tower_interval_ms && now - s_tower_last_toggle >= pdMS_TO_TICKS(tower_interval_ms)) {
        bool next_phase = !s_tower_phase;
        if (s_tower_state == LED_BLINK_DOUBLE) {
            s_tower_double_step = (uint8_t)((s_tower_double_step + 1U) % 4U);
            next_phase = s_tower_double_step == 0U || s_tower_double_step == 2U;
        }
        const esp_err_t err = do_write(tower_channel(s_tower_color), next_phase);
        if (err != ESP_OK) {
            if (first_error == ESP_OK) first_error = err;
            note_tick_result(first_error);
            return first_error;
        }
        s_tower_phase = next_phase;
        s_tower_last_toggle = now;
    }
    note_tick_result(first_error);
    return first_error;
}

esp_err_t set_tower_light(int color, LEDState_t state)
{
    const callbox_io_mapping_t *mapping = callbox_io_get_mapping();
    const uint8_t tower_mask = (uint8_t)((1u << mapping->tower_red) |
                                         (1u << mapping->tower_yellow) |
                                         (1u << mapping->tower_green));
    const bool initial_phase = state != LED_OFF && color != 0;
    const uint8_t set_mask = initial_phase ?
                             (uint8_t)(1u << tower_channel(color)) : 0U;

    /* Một giao dịch mask giúp tránh ba lần I2C + trạng thái tower trung gian. */
    const esp_err_t err = bsp_do_update_mask(tower_mask, set_mask);
    if (err != ESP_OK) return err;
    s_tower_color = color;
    s_tower_state = state;
    s_tower_phase = initial_phase;
    s_tower_double_step = 0U;
    s_tower_last_toggle = xTaskGetTickCount();
    return ESP_OK;
}

void buzzer_beep(int beep_count, int duration_ms)
{
    if (!s_buzzer_queue || beep_count <= 0 || duration_ms <= 0) return;
    const BuzzerCmd_t command = { .beep_count = beep_count, .duration_ms = duration_ms };
    if (xQueueSend(s_buzzer_queue, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Buzzer queue full; feedback skipped");
    }
}
