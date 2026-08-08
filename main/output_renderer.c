/**
 * @file output_renderer.c
 * @brief Ánh xạ snapshot ứng dụng bất biến (immutable) sang LED, tower
 *        và buzzer DO1.
 */
#include "output_renderer.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_control.h"

static const char *TAG = "OUTPUT_RENDERER";
static QueueHandle_t s_snapshot_queue;

static uint32_t renderer_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool time_before(uint32_t now_ms, uint32_t until_ms)
{
    return until_ms != 0U && (int32_t)(until_ms - now_ms) > 0;
}

static LEDState_t task_led_state(const app_output_snapshot_t *snapshot, int task,
                                 uint32_t now_ms)
{
    const int index = task - 1;
    if (time_before(now_ms, snapshot->task_error_until_ms[index])) return LED_FLASH_3;
    /* A slow blink is strictly the local "call sent, awaiting WCS" indication.
     * Once WCS confirms `accepted`, Mission becomes QUEUED and the task LED
     * changes to steady ON, just like ASSIGNED/LOCKED. */
    if (snapshot->call_pending[index]) {
        return LED_BLINK_SLOW;
    }
    return (snapshot->mission[index] == TASK_QUEUED ||
            snapshot->mission[index] == TASK_ASSIGNED ||
            snapshot->mission[index] == TASK_LOCKED) ? LED_ON : LED_OFF;
}

static void apply_feedback(const app_output_snapshot_t *snapshot, uint32_t *last_generation)
{
    if (snapshot->feedback_generation == *last_generation) return;
    *last_generation = snapshot->feedback_generation;
    switch (snapshot->feedback) {
    case OUTPUT_FEEDBACK_CALL_REQUESTED:
        buzzer_beep(1, 100);
        break;
    case OUTPUT_FEEDBACK_TASK_ASSIGNED:
        buzzer_beep(1, 100);
        break;
    case OUTPUT_FEEDBACK_CONFIG_SAVED:
        buzzer_beep(1, 120);
        break;
    case OUTPUT_FEEDBACK_CANCEL_ACKNOWLEDGED:
        buzzer_beep(1, 150);
        break;
    case OUTPUT_FEEDBACK_TRANSACTION_FAILED:
        buzzer_beep(1, 650);
        break;
    default:
        break;
    }
}

static void render_snapshot(const app_output_snapshot_t *snapshot)
{
    static LEDState_t last_button[3] = { LED_OFF, LED_OFF, LED_OFF };
    static int last_tower_color = -1;
    static LEDState_t last_tower_state = LED_OFF;
    static uint32_t last_feedback_generation;
    const uint32_t now_ms = renderer_now_ms();
    const LEDState_t desired_button[3] = {
        task_led_state(snapshot, 1, now_ms),
        task_led_state(snapshot, 2, now_ms),
        time_before(now_ms, snapshot->cancel_ack_until_ms) ? LED_FLASH_2 :
        snapshot->cancel_pending ? LED_BLINK_SLOW :
        (snapshot->mission[0] == TASK_QUEUED || snapshot->mission[0] == TASK_ASSIGNED ||
         snapshot->mission[1] == TASK_QUEUED || snapshot->mission[1] == TASK_ASSIGNED) ? LED_ON : LED_OFF,
    };

    for (int i = 0; i < 3; ++i) {
        if (desired_button[i] != last_button[i]) {
            set_button_led(i + 1, desired_button[i]);
            last_button[i] = desired_button[i];
        }
    }

    const bool comm_not_ready = snapshot->comm_state != COMM_READY;
    const bool mission_active = snapshot->call_pending[0] || snapshot->call_pending[1] ||
                                snapshot->cancel_pending || snapshot->mission[0] != TASK_IDLE ||
                                snapshot->mission[1] != TASK_IDLE;
    /* WCS is authoritative. Until its sync snapshot completes, Callbox must
     * visibly remain unavailable even when the MQTT transport is connected. */
    const int tower_color = comm_not_ready ? 1 :
                            snapshot->tower_warning == TOWER_WARNING_ERROR ? 1 :
                            snapshot->tower_warning == TOWER_WARNING_OVERDUE ? 2 :
                            mission_active ? 2 : 3;
    const LEDState_t tower_state = comm_not_ready ? LED_BLINK_SLOW :
                                   snapshot->tower_warning == TOWER_WARNING_ERROR ? LED_ON :
                                   snapshot->tower_warning == TOWER_WARNING_OVERDUE ? LED_BLINK_SLOW :
                                   LED_ON;
    if (tower_color != last_tower_color || tower_state != last_tower_state) {
        set_tower_light(tower_color, tower_state);
        last_tower_color = tower_color;
        last_tower_state = tower_state;
    }

    apply_feedback(snapshot, &last_feedback_generation);
}

static void output_renderer_task(void *arg)
{
    (void)arg;
    app_output_snapshot_t snapshot = {0};
    for (;;) {
        if (xQueueReceive(s_snapshot_queue, &snapshot, pdMS_TO_TICKS(20)) == pdTRUE) {
            render_snapshot(&snapshot);
        }
        /* Blink timing and physical output writes are owned by this task. */
        led_control_tick();
    }
}

void output_renderer_init(void)
{
    if (s_snapshot_queue) return;
    s_snapshot_queue = xQueueCreate(1, sizeof(app_output_snapshot_t));
    if (!s_snapshot_queue ||
        xTaskCreate(output_renderer_task, "output_renderer", 3072, NULL, 7, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot start output renderer");
        return;
    }
    ESP_LOGI(TAG, "Snapshot-only output renderer started");
}

void output_renderer_publish(const app_output_snapshot_t *snapshot)
{
    if (!s_snapshot_queue || !snapshot) return;
    (void)xQueueOverwrite(s_snapshot_queue, snapshot);
}
