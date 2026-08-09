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

static const char *TAG = "LED_CONTROL";

/* Queue lệnh buzzer nghiệp vụ — thuộc sở hữu riêng của led_control, không
 * còn là biến toàn cục của app_main. Độ sâu giữ nguyên 10 như baseline. */
static QueueHandle_t s_buzzer_queue = NULL;

#define BUZZER_QUEUE_DEPTH 10

static LEDState_t s_button_state[3] = { LED_OFF, LED_OFF, LED_OFF };
static bool s_button_phase[3];
static TickType_t s_button_last_toggle[3];
static uint8_t s_button_flash_remaining[3];
static int s_tower_color;
static LEDState_t s_tower_state = LED_OFF;
static bool s_tower_phase;
static TickType_t s_tower_last_toggle;

#define BUTTON_BLINK_SLOW_MS 500U
#define BUTTON_BLINK_FAST_MS 150U
#define TOWER_BLINK_SLOW_MS  500U
#define TOWER_BLINK_FAST_MS  250U

static void do_write(bsp_do_channel_t channel, bool active)
{
    const esp_err_t err = bsp_do_write(channel, active);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DO%d write failed: %s", channel + 1, esp_err_to_name(err));
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
            do_write(callbox_io_get_mapping()->buzzer, true);
            vTaskDelay(pdMS_TO_TICKS(command.duration_ms));
            do_write(callbox_io_get_mapping()->buzzer, false);
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

void led_control_init(void)
{
    if (s_buzzer_queue && xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 6, NULL) == pdPASS) {
        ESP_LOGI(TAG, "Output primitives ready via BSP");
    } else {
        ESP_LOGE(TAG, "Buzzer queue/task unavailable");
    }
}

void set_button_led(int button_id, LEDState_t state)
{
    if (button_id < 1 || button_id > 3) return;
    const int index = button_id - 1;
    s_button_state[index] = state;
    s_button_phase[index] = state != LED_OFF;
    s_button_flash_remaining[index] = state == LED_FLASH_2 ? 2U :
                                      state == LED_FLASH_3 ? 3U : 0U;
    s_button_last_toggle[index] = xTaskGetTickCount();
    do_write(button_channel(button_id), s_button_phase[index]);
}

void led_control_tick(void)
{
    const TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < 3; ++i) {
        const uint32_t interval_ms = (s_button_state[i] == LED_BLINK_FAST ||
                                      s_button_state[i] == LED_FLASH_2 ||
                                      s_button_state[i] == LED_FLASH_3) ? BUTTON_BLINK_FAST_MS :
                                     s_button_state[i] == LED_BLINK_SLOW ? BUTTON_BLINK_SLOW_MS : 0U;
        if (!interval_ms || now - s_button_last_toggle[i] < pdMS_TO_TICKS(interval_ms)) continue;
        if ((s_button_state[i] == LED_FLASH_2 || s_button_state[i] == LED_FLASH_3) &&
            s_button_phase[i] && --s_button_flash_remaining[i] == 0U) {
            s_button_phase[i] = false;
            s_button_state[i] = LED_OFF;
            s_button_last_toggle[i] = now;
            do_write(button_channel(i + 1), false);
            continue;
        }
        s_button_phase[i] = !s_button_phase[i];
        s_button_last_toggle[i] = now;
        do_write(button_channel(i + 1), s_button_phase[i]);
    }

    const uint32_t tower_interval_ms = s_tower_state == LED_BLINK_FAST ?
                                       TOWER_BLINK_FAST_MS :
                                       s_tower_state == LED_BLINK_SLOW ? TOWER_BLINK_SLOW_MS : 0U;
    if (tower_interval_ms && now - s_tower_last_toggle >= pdMS_TO_TICKS(tower_interval_ms)) {
        s_tower_phase = !s_tower_phase;
        s_tower_last_toggle = now;
        do_write(tower_channel(s_tower_color), s_tower_phase);
    }
}

void set_tower_light(int color, LEDState_t state)
{
    const callbox_io_mapping_t *mapping = callbox_io_get_mapping();
    do_write(mapping->tower_red, false);
    do_write(mapping->tower_yellow, false);
    do_write(mapping->tower_green, false);
    s_tower_color = color;
    s_tower_state = state;
    s_tower_phase = state != LED_OFF && color != 0;
    s_tower_last_toggle = xTaskGetTickCount();
    if (state == LED_OFF || color == 0) return;
    do_write(tower_channel(color), s_tower_phase);
}

void buzzer_beep(int beep_count, int duration_ms)
{
    if (!s_buzzer_queue || beep_count <= 0 || duration_ms <= 0) return;
    const BuzzerCmd_t command = { .beep_count = beep_count, .duration_ms = duration_ms };
    if (xQueueSend(s_buzzer_queue, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Buzzer queue full; feedback skipped");
    }
}
