/** @file state_machine.c @brief Mission Manager: chủ sở hữu duy nhất trạng thái nghiệp vụ. */
#include "state_machine.h"
#include "app_event_queue.h"
#include "button_gate.h"
#include "callbox_mqtt.h"
#include "io_handler.h"
#include "output_renderer.h"
#include "network_link.h"
#include "network_status_task.h"
#include "sequence_service.h"
#include "status.h"
#include "wifi_init.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "MISSION";
static char s_agv_id[2][32];
static uint32_t s_sync_sequence;
static mission_transaction_t s_sync_transaction;
static bool s_cancel_hold_active;
static bool s_cancel_hold_consumed;
static uint32_t s_cancel_hold_started_ms;

#define CANCEL_RESCUE_HOLD_MS 5000U

static void begin_wcs_sync(void);

static uint32_t mission_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Phép trừ không dấu cố tình xử lý việc tràn (wrap) millisecond của
 * esp_timer. */
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static const char *state_name(TaskState_t state)
{
    static const char *const names[] = {"IDLE", "QUEUED", "ASSIGNED", "LOCKED", "COMPLETED"};
    return state <= TASK_COMPLETED ? names[state] : "UNKNOWN";
}

TaskState_t get_task_state(int task_id)
{
    return task_id >= 1 && task_id <= 2 ? status.Mission[task_id - 1] : TASK_IDLE;
}

const char *get_task_state_str(int task_id) { return state_name(get_task_state(task_id)); }

const char *get_comm_state_str(void)
{
    switch (status.CommState) {
    case COMM_READY: return "ready";
    case COMM_SYNCING: return "syncing";
    default: return "offline";
    }
}

static void mission_set_state(int task_id, TaskState_t state, uint32_t timestamp)
{
    status_set_mission(task_id, state, timestamp);
    if (state == TASK_IDLE) {
        memset(s_agv_id[task_id - 1], 0, sizeof(s_agv_id[0]));
    }
    ESP_LOGI(TAG, "Task %d -> %s", task_id, state_name(state));
}

void reset_task(int task_id) { mission_set_state(task_id, TASK_IDLE, 0); }

static bool task_cancelable(int task_id)
{
    const TaskState_t state = get_task_state(task_id);
    return state == TASK_QUEUED || state == TASK_ASSIGNED;
}

static void request_call(int task_id, uint32_t timestamp)
{
    if (status.CommState != COMM_READY || !network_link_is_connected() || !mqtt_is_connected() || status.Cancel.pending ||
        get_task_state(task_id) != TASK_IDLE || status.Call[task_id - 1].pending) {
        ESP_LOGI(TAG, "CALL task %d rejected by admission conditions", task_id);
        return;
    }
    uint32_t sequence;
    if (sequence_next(&sequence) != ESP_OK) return;
    status_start_call(task_id, sequence, mission_now_ms(), timestamp);
    mqtt_publish_call(task_id, sequence, timestamp);
    /* Trạng thái pending chỉ là cục bộ; Mission vẫn IDLE cho tới khi WCS
     * chấp nhận. */
    status_request_feedback(OUTPUT_FEEDBACK_CALL_REQUESTED);
    ESP_LOGI(TAG, "CALL task %d pending seq=%lu", task_id, (unsigned long)sequence);
}

static void request_cancel(uint32_t timestamp)
{
    if (status.CommState != COMM_READY || status.Cancel.pending ||
        !network_link_is_connected() || !mqtt_is_connected()) return;
    int target = 0;
    for (int task = 1; task <= 2; ++task) {
        if (task_cancelable(task) && (!target || status.CallSequence[task - 1] > status.CallSequence[target - 1])) {
            target = task;
        }
    }
    if (!target) {
        /* Không còn task nào để hủy (IDLE hoặc LOCKED): phát một tiếng bíp dài
         * báo thao tác không hợp lệ như quy ước. */
        status_request_feedback(OUTPUT_FEEDBACK_TRANSACTION_FAILED);
        return;
    }
    uint32_t sequence;
    if (sequence_next(&sequence) != ESP_OK) return;
    status_start_cancel(target, sequence, mission_now_ms(), timestamp);
    mqtt_publish_cancel(target, sequence, timestamp);
    ESP_LOGI(TAG, "CANCEL task %d pending seq=%lu", target, (unsigned long)sequence);
}

void handle_button_press(int button_id, uint32_t timestamp)
{
    if (button_id == 1 || button_id == 2) request_call(button_id, timestamp);
    else if (button_id == 3) request_cancel(timestamp);
}

static bool command_matches_call(int task_id, uint32_t ref_seq)
{
    return task_id >= 1 && task_id <= 2 && ref_seq != 0 &&
           status.CallSequence[task_id - 1] == ref_seq;
}

void handle_mqtt_command(const char *cmd, int task, const char *agv_id, uint32_t timestamp)
{
    if (!cmd || task < 1 || task > 2) return;
    /* API tương thích; điểm vào Phase 4 dùng app_event_t bên dưới với
     * ref_seq. */
    (void)agv_id;
    ESP_LOGW(TAG, "Legacy MQTT command ignored without ref_seq: %s", cmd);
    (void)timestamp;
}

static void handle_wcs_command(const protocol_command_t *cmd)
{
    const int task = cmd->task;
    if (cmd->type == PROTOCOL_CMD_SYNC) {
        if (status.CommState != COMM_SYNCING || cmd->ref_seq != s_sync_sequence) {
            ESP_LOGW(TAG, "Ignoring unmatched sync ref=%lu", (unsigned long)cmd->ref_seq);
            return;
        }
        for (int i = 0; i < 2; ++i) {
            const int sync_task = i + 1;
            status_clear_call(sync_task);
            status.CallSequence[i] = cmd->sync_call_seq[i];
            strncpy(s_agv_id[i], cmd->sync_agv_id[i], sizeof(s_agv_id[i]) - 1);
            s_agv_id[i][sizeof(s_agv_id[i]) - 1] = '\0';
            mission_set_state(sync_task, cmd->sync_state[i], cmd->timestamp);
        }
        status_clear_cancel();
        status_set_tower_warning(0, TOWER_WARNING_NONE);
        memset(&s_sync_transaction, 0, sizeof(s_sync_transaction));
        status_set_comm_state(COMM_READY);
        ESP_LOGI(TAG, "WCS sync accepted seq=%lu; Callbox ready", (unsigned long)s_sync_sequence);
        return;
    }
    if (task < 1 || task > 2) return;
    if (cmd->type == PROTOCOL_CMD_CANCEL_ACK) {
        if (!status.Cancel.pending || status.CancelTarget != task || cmd->ref_seq != status.Cancel.seq) {
            ESP_LOGW(TAG, "Ignoring unmatched cancel_ack task=%d ref=%lu", task, (unsigned long)cmd->ref_seq);
            return;
        }
        mission_set_state(task, TASK_IDLE, cmd->timestamp);
        status_clear_cancel();
        status_clear_tower_warning_for_task(task);
        status_set_cancel_ack_feedback(mission_now_ms() + CANCEL_ACK_FLASH_WINDOW_MS);
        status_request_feedback(OUTPUT_FEEDBACK_CANCEL_ACKNOWLEDGED);
        return;
    }
    /* `rejected` có thể xác nhận một trong hai loại giao dịch. Một call và
     * một cancel sau đó cố tình dùng số thứ tự khác nhau, nên chỉ kiểm tra
     * CallSequence thôi sẽ âm thầm bỏ qua một cancel bị từ chối như
     * {type:"rejected", task:1, ref_seq:105, reason:"locked"}. */
    if (cmd->type == PROTOCOL_CMD_REJECTED) {
        const bool rejects_call = status.Call[task - 1].pending &&
                                  cmd->ref_seq == status.Call[task - 1].seq;
        const bool rejects_cancel = status.Cancel.pending &&
                                    status.CancelTarget == task &&
                                    cmd->ref_seq == status.Cancel.seq;
        if (!rejects_call && !rejects_cancel) {
            ESP_LOGW(TAG, "Ignoring unmatched rejected task=%d ref=%lu", task,
                     (unsigned long)cmd->ref_seq);
            return;
        }

        if (rejects_cancel) {
            /* WCS vẫn có thẩm quyền: không đổi mission sang IDLE. Chỉ xóa giao
             * dịch cancel cục bộ này, báo cho người vận hành, rồi đồng bộ lại
             * (WCS có thể báo LOCKED sau reason=locked). */
            status_clear_cancel();
            status_request_feedback(OUTPUT_FEEDBACK_TRANSACTION_FAILED);
            ESP_LOGW(TAG, "CANCEL rejected task=%d ref=%lu reason=%s",
                     task, (unsigned long)cmd->ref_seq,
                     protocol_reject_reason_name(cmd->reason));
            /* `locked`, `duplicate`, và `no_task` ngụ ý Callbox và WCS có
             * thể không khớp về snapshot mission, nên đồng bộ lại.
             * `wcs_busy` không đổi trạng thái mission: người vận hành có thể
             * thử lại sau mà không cần bắt đầu giao dịch khác ngay. */
            if (cmd->reason != REJECT_REASON_WCS_BUSY) begin_wcs_sync();
            return;
        }

        status_clear_call(task);
        mission_set_state(task, TASK_IDLE, cmd->timestamp);
        status_set_tower_warning(task, TOWER_WARNING_ERROR);
        status_set_task_error(task, mission_now_ms() + TASK_REJECT_FLASH_WINDOW_MS);
        status_request_feedback(OUTPUT_FEEDBACK_TRANSACTION_FAILED);
        ESP_LOGW(TAG, "CALL rejected task=%d ref=%lu reason=%s", task,
                 (unsigned long)cmd->ref_seq,
                 protocol_reject_reason_name(cmd->reason));
        /* CALL trùng lặp có thể nghĩa là WCS đã cam kết một lần giao hàng
         * trước đó. Đồng bộ lại thay vì tin vào hiển thị lỗi cục bộ. */
        if (cmd->reason == REJECT_REASON_DUPLICATE) begin_wcs_sync();
        return;
    }

    if (!command_matches_call(task, cmd->ref_seq)) {
        ESP_LOGW(TAG, "Ignoring stale WCS cmd task=%d ref=%lu", task, (unsigned long)cmd->ref_seq);
        return;
    }
    if (cmd->type == PROTOCOL_CMD_ACCEPTED) {
        if (!status.Call[task - 1].pending) return;
        status_clear_call(task);
        status_clear_task_error(task);
        status_clear_tower_warning_for_task(task);
        mission_set_state(task, TASK_QUEUED, cmd->timestamp);
    } else if (cmd->type == PROTOCOL_CMD_ASSIGNED) {
        status_clear_call(task);
        status_clear_task_error(task);
        status_clear_tower_warning_for_task(task);
        strncpy(s_agv_id[task - 1], cmd->agv_id, sizeof(s_agv_id[0]) - 1);
        mission_set_state(task, TASK_ASSIGNED, cmd->timestamp);
        status_request_feedback(OUTPUT_FEEDBACK_TASK_ASSIGNED);
    } else if (cmd->type == PROTOCOL_CMD_LOCKED) {
        mission_set_state(task, TASK_LOCKED, cmd->timestamp);
    } else if (cmd->type == PROTOCOL_CMD_COMPLETED) {
        mission_set_state(task, TASK_IDLE, cmd->timestamp);
        status_clear_tower_warning_for_task(task);
    } else if (cmd->type == PROTOCOL_CMD_OVERDUE) {
        status_set_tower_warning(task, TOWER_WARNING_OVERDUE);
    }
}

static void begin_wcs_sync(void)
{
    uint32_t sequence;
    status_set_comm_state(COMM_SYNCING);
    if (sequence_next(&sequence) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot allocate sync sequence");
        status_set_comm_state(COMM_OFFLINE);
        return;
    }
    s_sync_sequence = sequence;
    const uint32_t now_ms = mission_now_ms();
    s_sync_transaction.pending = true;
    s_sync_transaction.seq = sequence;
    s_sync_transaction.retry_count = 0;
    s_sync_transaction.retry_at_ms = now_ms + MISSION_RETRY_INTERVAL_MS;
    s_sync_transaction.deadline_ms = now_ms + MISSION_TRANSACTION_TIMEOUT_MS;
    s_sync_transaction.timestamp = (uint32_t)time(NULL);
    mqtt_publish_sync_request(sequence, s_sync_transaction.timestamp);
    ESP_LOGI(TAG, "Requested WCS mission sync seq=%lu", (unsigned long)sequence);
}

static void tick_sync(void)
{
    if (status.CommState != COMM_SYNCING || !s_sync_transaction.pending) return;
    const uint32_t now_ms = mission_now_ms();
    if (time_reached(now_ms, s_sync_transaction.deadline_ms)) {
        ESP_LOGW(TAG, "WCS sync seq=%lu timed out; requesting a fresh snapshot",
                 (unsigned long)s_sync_transaction.seq);
        memset(&s_sync_transaction, 0, sizeof(s_sync_transaction));
        if (mqtt_is_connected()) begin_wcs_sync();
    } else if (s_sync_transaction.retry_count < MISSION_MAX_RETRIES &&
               time_reached(now_ms, s_sync_transaction.retry_at_ms) && mqtt_is_connected()) {
        mqtt_publish_sync_request(s_sync_transaction.seq, s_sync_transaction.timestamp);
        s_sync_transaction.retry_count++;
        s_sync_transaction.retry_at_ms = now_ms + MISSION_RETRY_INTERVAL_MS;
        ESP_LOGW(TAG, "WCS sync retry seq=%lu attempt=%u",
                 (unsigned long)s_sync_transaction.seq, s_sync_transaction.retry_count);
    }
}

static void publish_output_snapshot(void)
{
    const app_output_snapshot_t snapshot = {
        .mission = { status.Mission[0], status.Mission[1] },
        .call_pending = { status.Call[0].pending, status.Call[1].pending },
        .cancel_pending = status.Cancel.pending,
        .comm_state = status.CommState,
        .tower_warning = status.TowerWarning,
        .task_error_until_ms = { status.TaskErrorUntilMs[0], status.TaskErrorUntilMs[1] },
        .cancel_ack_until_ms = status.CancelAckUntilMs,
        .feedback = status.Feedback,
        .feedback_generation = status.FeedbackGeneration,
    };
    output_renderer_publish(&snapshot);
}

static void tick_transactions(void)
{
    const uint32_t now_ms = mission_now_ms();

    for (int task = 1; task <= 2; ++task) {
        mission_transaction_t *const call = (mission_transaction_t *)&status.Call[task - 1];
        if (!call->pending) continue;

        if (time_reached(now_ms, call->deadline_ms)) {
            ESP_LOGW(TAG, "CALL task %d timed out seq=%lu after %u retries",
                     task, (unsigned long)call->seq, call->retry_count);
            status_clear_call(task);
            status_set_task_error(task, now_ms + 2000U);
            status_request_feedback(OUTPUT_FEEDBACK_TRANSACTION_FAILED);
        } else if (call->retry_count < MISSION_MAX_RETRIES &&
                   time_reached(now_ms, call->retry_at_ms) &&
                   network_link_is_connected() && mqtt_is_connected()) {
            /* Truyền lại cũng là một giao dịch: không bao giờ tiêu thụ seq
             * mới. */
            mqtt_publish_call(task, call->seq, call->timestamp);
            status_note_call_retry(task, now_ms + MISSION_RETRY_INTERVAL_MS);
            ESP_LOGW(TAG, "CALL retry task %d seq=%lu attempt=%u",
                     task, (unsigned long)call->seq, call->retry_count);
        }
    }

    mission_transaction_t *const cancel = (mission_transaction_t *)&status.Cancel;
    if (!cancel->pending) return;
    if (time_reached(now_ms, cancel->deadline_ms)) {
        ESP_LOGW(TAG, "CANCEL task %u timed out seq=%lu after %u retries",
                 status.CancelTarget, (unsigned long)cancel->seq, cancel->retry_count);
        status_clear_cancel();
        status_request_feedback(OUTPUT_FEEDBACK_TRANSACTION_FAILED);
    } else if (cancel->retry_count < MISSION_MAX_RETRIES &&
               time_reached(now_ms, cancel->retry_at_ms) &&
               network_link_is_connected() && mqtt_is_connected()) {
        mqtt_publish_cancel(status.CancelTarget, cancel->seq, cancel->timestamp);
        status_note_cancel_retry(now_ms + MISSION_RETRY_INTERVAL_MS);
        ESP_LOGW(TAG, "CANCEL retry task %u seq=%lu attempt=%u", status.CancelTarget,
                 (unsigned long)cancel->seq, cancel->retry_count);
    }
}

void state_machine_init(void)
{
    status_init();
    button_gate_reset();
    status_set_comm_state(COMM_OFFLINE);
    s_cancel_hold_active = false;
    s_cancel_hold_consumed = false;
    ESP_LOGI(TAG, "Mission Manager initialized (single business-state owner)");
}

static void handle_button_event(const ButtonMsg_t *button)
{
    const bool accepted_press = button_gate_accept(button);

    if (button->button_id != 3) {
        if (accepted_press && status.Button[button->button_id - 1].pressed_edge) {
            handle_button_press(button->button_id, button->timestamp);
        }
        return;
    }

    /* Nhấn Cancel ngắn chỉ được cam kết sau khi nhả nút. Điều này ngăn việc
     * giữ nút 5 giây để kích hoạt Rescue AP vô tình cũng gửi lệnh cancel. */
    if (button->state == BTN_PRESSED && accepted_press) {
        s_cancel_hold_active = true;
        s_cancel_hold_consumed = false;
        s_cancel_hold_started_ms = mission_now_ms();
    } else if (button->state == BTN_RELEASED && s_cancel_hold_active) {
        if (!s_cancel_hold_consumed) request_cancel(button->timestamp);
        s_cancel_hold_active = false;
    }
}

static void tick_cancel_rescue_hold(void)
{
    if (!s_cancel_hold_active || s_cancel_hold_consumed || !status.Button[2].level ||
        !time_reached(mission_now_ms(), s_cancel_hold_started_ms + CANCEL_RESCUE_HOLD_MS)) {
        return;
    }

    bool rescue_enabled = false;
    const esp_err_t err = wifi_toggle_rescue_ap(&rescue_enabled);
    s_cancel_hold_consumed = true;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rescue AP toggle failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "Rescue AP %s by 5-second Cancel hold",
             rescue_enabled ? "enabled" : "disabled");
    network_status_notify_rescue_ap_changed(rescue_enabled);
}

void state_machine_task(void *arg)
{
    (void)arg;
    ButtonMsg_t button;
    app_event_t event;
    QueueHandle_t button_queue = io_handler_get_button_event_queue();
    while (true) {
        while (xQueueReceive(button_queue, &button, 0) == pdTRUE) {
            handle_button_event(&button);
        }
        while (app_event_receive(&event, 0)) {
            if (event.type == APP_EVENT_WCS_COMMAND) {
                handle_wcs_command(&event.command);
            } else if (event.type == APP_EVENT_MQTT_CONNECTED) {
                begin_wcs_sync();
            } else if (event.type == APP_EVENT_MQTT_DISCONNECTED) {
                s_sync_sequence = 0;
                memset(&s_sync_transaction, 0, sizeof(s_sync_transaction));
                status_set_comm_state(COMM_OFFLINE);
                ESP_LOGW(TAG, "MQTT offline; button admission paused until WCS sync");
            }
        }
        tick_transactions();
        tick_sync();
        tick_cancel_rescue_hold();
        publish_output_snapshot();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
