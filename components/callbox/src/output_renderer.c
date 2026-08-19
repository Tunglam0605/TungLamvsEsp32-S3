/**
 * @file output_renderer.c
 * @brief Ánh xạ snapshot ứng dụng bất biến (immutable) sang LED, tower
 *        và buzzer DO1.
 */
#include "output_renderer.h"

#include "bsp_do.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "health_monitor.h"
#include "led_control.h"

static const char *TAG = "OUTPUT_RENDERER";
static QueueHandle_t s_snapshot_queue;

#define OUTPUT_RECONCILE_INTERVAL_MS 5000U
#define OUTPUT_RETRY_INITIAL_MS       100U
#define OUTPUT_RETRY_MAX_MS          5000U
#define OUTPUT_BUS_RECOVERY_THRESHOLD   3U
#define OUTPUT_BUS_RECOVERY_INTERVAL_MS 30000U
#define NETWORK_ALERT_REMINDER_MS       60000U

typedef struct {
    LEDState_t button[3];
    int tower_color;
    LEDState_t tower_state;
    bool button_valid[3];
    bool tower_valid;
    uint32_t next_retry_ms;
    uint32_t retry_delay_ms;
    uint32_t last_reconcile_ms;
    uint32_t failure_count;
    uint32_t last_error_log_ms;
    uint32_t last_bus_recovery_ms;
} output_apply_state_t;

static output_apply_state_t s_apply = {
    .button = { LED_OFF, LED_OFF, LED_OFF },
    .tower_color = -1,
    .tower_state = LED_OFF,
    .retry_delay_ms = OUTPUT_RETRY_INITIAL_MS,
};

typedef enum {
    TRANSPORT_ALERT_NO_UPLINK = 0,
    TRANSPORT_ALERT_MQTT_OFFLINE,
    TRANSPORT_ALERT_SYNCING,
    TRANSPORT_ALERT_READY,
} transport_alert_t;

static struct {
    bool initialized;
    transport_alert_t state;
    uint32_t last_beep_ms;
} s_transport_alert;

static uint32_t renderer_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool time_before(uint32_t now_ms, uint32_t until_ms)
{
    return until_ms != 0U && (int32_t)(until_ms - now_ms) > 0;
}

static transport_alert_t transport_alert_from_snapshot(const app_output_snapshot_t *snapshot)
{
    if (!snapshot->uplink_available) return TRANSPORT_ALERT_NO_UPLINK;
    if (!snapshot->mqtt_connected) return TRANSPORT_ALERT_MQTT_OFFLINE;
    if (snapshot->comm_state != COMM_READY) return TRANSPORT_ALERT_SYNCING;
    return TRANSPORT_ALERT_READY;
}

/* The tower buzzer is relay-driven: distinguish network faults by pulse count
 * and duration, never frequency.  It signals transitions and sparse reminders
 * only, so a persistent fault does not create continuous factory noise. */
static void apply_transport_buzzer(transport_alert_t state, uint32_t now_ms)
{
    if (!s_transport_alert.initialized) {
        s_transport_alert.initialized = true;
        s_transport_alert.state = state;
        s_transport_alert.last_beep_ms = now_ms;
        return;
    }

    const bool changed = state != s_transport_alert.state;
    const bool reminder_due = !changed && state != TRANSPORT_ALERT_READY &&
                              (uint32_t)(now_ms - s_transport_alert.last_beep_ms) >=
                                  NETWORK_ALERT_REMINDER_MS;
    if (!changed && !reminder_due) return;

    if (state == TRANSPORT_ALERT_NO_UPLINK) {
        buzzer_beep(1, 700);
    } else if (state == TRANSPORT_ALERT_MQTT_OFFLINE) {
        buzzer_beep(2, 120);
    } else if (state == TRANSPORT_ALERT_READY && changed) {
        buzzer_beep(2, 100);
    }
    s_transport_alert.state = state;
    s_transport_alert.last_beep_ms = now_ms;
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

static bool output_retry_due(uint32_t now_ms)
{
    return s_apply.next_retry_ms == 0U ||
           (int32_t)(now_ms - s_apply.next_retry_ms) >= 0;
}

static void output_note_apply_result(bool success, esp_err_t first_error,
                                     uint32_t now_ms)
{
    if (success) {
        if (s_apply.failure_count != 0U) {
            ESP_LOGI(TAG, "Output reconciliation recovered after %lu failed pass(es)",
                     (unsigned long)s_apply.failure_count);
        }
        s_apply.failure_count = 0U;
        s_apply.retry_delay_ms = OUTPUT_RETRY_INITIAL_MS;
        s_apply.next_retry_ms = 0U;
        return;
    }

    s_apply.failure_count++;
    const uint32_t scheduled_delay_ms = s_apply.retry_delay_ms;
    s_apply.next_retry_ms = now_ms + scheduled_delay_ms;
    if (s_apply.retry_delay_ms < OUTPUT_RETRY_MAX_MS) {
        uint32_t next = s_apply.retry_delay_ms * 2U;
        s_apply.retry_delay_ms = next > OUTPUT_RETRY_MAX_MS ? OUTPUT_RETRY_MAX_MS : next;
    }
    if (s_apply.failure_count == 1U ||
        (int32_t)(now_ms - s_apply.last_error_log_ms) >= 5000) {
        ESP_LOGW(TAG, "Output apply failed (%lu consecutive); retry in %lu ms: %s",
                 (unsigned long)s_apply.failure_count,
                 (unsigned long)scheduled_delay_ms,
                 esp_err_to_name(first_error));
        s_apply.last_error_log_ms = now_ms;
    }

    /* Sau vài pass lỗi liên tiếp, thử reset FSM của controller I2C. Việc phục
     * hồi được rate-limit 30 giây; renderer vẫn giữ desired snapshot và sẽ
     * ghi lại toàn bộ output ở pass kế tiếp, không mutate state nghiệp vụ. */
    if (s_apply.failure_count >= OUTPUT_BUS_RECOVERY_THRESHOLD &&
        (s_apply.last_bus_recovery_ms == 0U ||
         (int32_t)(now_ms - s_apply.last_bus_recovery_ms) >=
             (int32_t)OUTPUT_BUS_RECOVERY_INTERVAL_MS)) {
        s_apply.last_bus_recovery_ms = now_ms;
        const esp_err_t recovery = bsp_do_recover_bus();
        if (recovery == ESP_OK) {
            ESP_LOGW(TAG, "I2C bus reset requested after repeated output failures");
            for (int i = 0; i < 3; ++i) s_apply.button_valid[i] = false;
            s_apply.tower_valid = false;
            s_apply.next_retry_ms = now_ms + OUTPUT_RETRY_INITIAL_MS;
        } else {
            ESP_LOGE(TAG, "I2C bus recovery failed: %s", esp_err_to_name(recovery));
        }
    }
}

static void render_snapshot(const app_output_snapshot_t *snapshot)
{
    static uint32_t last_feedback_generation;
    const uint32_t now_ms = renderer_now_ms();
    const bool periodic_reconcile =
        (int32_t)(now_ms - s_apply.last_reconcile_ms) >= OUTPUT_RECONCILE_INTERVAL_MS;
    const LEDState_t desired_button[3] = {
        task_led_state(snapshot, 1, now_ms),
        task_led_state(snapshot, 2, now_ms),
        time_before(now_ms, snapshot->cancel_ack_until_ms) ? LED_FLASH_2 :
        snapshot->cancel_pending ? LED_BLINK_SLOW :
        (snapshot->mission[0] == TASK_QUEUED || snapshot->mission[0] == TASK_ASSIGNED ||
         snapshot->mission[1] == TASK_QUEUED || snapshot->mission[1] == TASK_ASSIGNED) ? LED_ON : LED_OFF,
    };

    const bool mission_active = snapshot->call_pending[0] || snapshot->call_pending[1] ||
                                snapshot->cancel_pending || snapshot->mission[0] != TASK_IDLE ||
                                snapshot->mission[1] != TASK_IDLE;
    /* A WCS-confirmed mission must remain visually dominant over an error from
     * the other task.  Example: Task 1 is ASSIGNED while Task 2 is rejected;
     * the tower stays yellow for Task 1 and Task 2 reports its error locally
     * through its button LED and the buzzer. */
    const bool confirmed_mission_active =
        snapshot->mission[0] == TASK_QUEUED || snapshot->mission[0] == TASK_ASSIGNED ||
        snapshot->mission[0] == TASK_LOCKED || snapshot->mission[1] == TASK_QUEUED ||
        snapshot->mission[1] == TASK_ASSIGNED || snapshot->mission[1] == TASK_LOCKED;
    const transport_alert_t transport_alert = transport_alert_from_snapshot(snapshot);
    apply_transport_buzzer(transport_alert, now_ms);
    /* Transport faults have priority over Mission output: red fast means no
     * uplink, red slow means broker unreachable, red double means WCS sync. */
    const int tower_color = transport_alert == TRANSPORT_ALERT_READY ?
                            confirmed_mission_active ? 2 :
                            snapshot->tower_warning == TOWER_WARNING_ERROR ? 1 :
                            snapshot->tower_warning == TOWER_WARNING_OVERDUE ? 2 :
                            mission_active ? 2 : 3 : 1;
    const LEDState_t tower_state = transport_alert == TRANSPORT_ALERT_NO_UPLINK ?
                                       LED_BLINK_FAST :
                                   transport_alert == TRANSPORT_ALERT_MQTT_OFFLINE ?
                                       LED_BLINK_SLOW :
                                   transport_alert == TRANSPORT_ALERT_SYNCING ?
                                       LED_BLINK_DOUBLE :
                                   confirmed_mission_active &&
                                       snapshot->tower_warning == TOWER_WARNING_OVERDUE ? LED_BLINK_SLOW :
                                   confirmed_mission_active ? LED_ON :
                                   snapshot->tower_warning == TOWER_WARNING_ERROR ? LED_ON :
                                   snapshot->tower_warning == TOWER_WARNING_OVERDUE ? LED_BLINK_SLOW :
                                   LED_ON;
    bool all_applied = true;
    esp_err_t first_error = ESP_OK;
    if (output_retry_due(now_ms)) {
        for (int i = 0; i < 3; ++i) {
            if (!s_apply.button_valid[i] || desired_button[i] != s_apply.button[i] ||
                periodic_reconcile) {
                const esp_err_t err = set_button_led(i + 1, desired_button[i]);
                if (err == ESP_OK) {
                    s_apply.button[i] = desired_button[i];
                    s_apply.button_valid[i] = true;
                } else {
                    s_apply.button_valid[i] = false;
                    all_applied = false;
                    if (first_error == ESP_OK) first_error = err;
                }
            }
        }

        if (!s_apply.tower_valid || tower_color != s_apply.tower_color ||
            tower_state != s_apply.tower_state || periodic_reconcile) {
            const esp_err_t err = set_tower_light(tower_color, tower_state);
            if (err == ESP_OK) {
                s_apply.tower_color = tower_color;
                s_apply.tower_state = tower_state;
                s_apply.tower_valid = true;
            } else {
                s_apply.tower_valid = false;
                all_applied = false;
                if (first_error == ESP_OK) first_error = err;
            }
        }
        if (periodic_reconcile) s_apply.last_reconcile_ms = now_ms;
        output_note_apply_result(all_applied, first_error, now_ms);
    }

    apply_feedback(snapshot, &last_feedback_generation);
}

static void output_renderer_task(void *arg)
{
    (void)arg;
    app_output_snapshot_t snapshot = {0};
    bool have_snapshot = false;
    health_monitor_check_in(HEALTH_TASK_OUTPUT_RENDERER);
    for (;;) {
        if (xQueueReceive(s_snapshot_queue, &snapshot, pdMS_TO_TICKS(20)) == pdTRUE) {
            have_snapshot = true;
        }
        /* Blink timing and physical output writes are owned by this task. */
        (void)led_control_tick();
        /* Render cả khi không có snapshot mới: deadline flash có thể tự hết và
         * trạng thái steady cần được reconcile định kỳ sau nhiễu I2C. */
        if (have_snapshot) render_snapshot(&snapshot);
        health_monitor_check_in(HEALTH_TASK_OUTPUT_RENDERER);
    }
}

esp_err_t output_renderer_init(void)
{
    if (s_snapshot_queue) return ESP_OK;
    s_snapshot_queue = xQueueCreate(1, sizeof(app_output_snapshot_t));
    if (!s_snapshot_queue) {
        ESP_LOGE(TAG, "Cannot start output renderer");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(output_renderer_task, "output_renderer", 3072, NULL, 7, NULL) != pdPASS) {
        vQueueDelete(s_snapshot_queue);
        s_snapshot_queue = NULL;
        ESP_LOGE(TAG, "Cannot start output renderer");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Snapshot-only output renderer started");
    return ESP_OK;
}

void output_renderer_publish(const app_output_snapshot_t *snapshot)
{
    if (!s_snapshot_queue || !snapshot) return;
    (void)xQueueOverwrite(s_snapshot_queue, snapshot);
}
