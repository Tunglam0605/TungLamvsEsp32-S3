/** @file state_machine.c @brief Mission Manager: chủ sở hữu duy nhất trạng thái nghiệp vụ. */
#include "state_machine.h"
#include "app_event_queue.h"
#include "button_gate.h"
#include "callbox_mqtt.h"
#include "io_handler.h"
#include "health_monitor.h"
#include "output_renderer.h"
#include "network_link.h"
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
#define SYNC_RETRY_INITIAL_MS 5000U
#define SYNC_RETRY_MAX_MS     300000U
#define SYNC_RETRY_QUEUE_BUSY_MS 1000U
#define TRANSACTION_QUEUE_BUSY_RETRY_MS 1000U
#define STATE_MAX_BUTTON_EVENTS_PER_CYCLE 8U
#define STATE_MAX_APP_EVENTS_PER_CYCLE    8U

static uint32_t s_sync_retry_interval_ms;
static bool s_sync_waiting_for_reconnect;
static bool s_reconcile_after_transaction_timeout;
static uint32_t s_active_session_epoch;

static void begin_wcs_sync(void);

typedef enum {
    TRANSITION_REJECT = 0,
    TRANSITION_NOOP,
    TRANSITION_APPLY,
} transition_decision_t;

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
    if (task_id < 1 || task_id > 2) return TASK_IDLE;
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);
    return snapshot.Mission[task_id - 1];
}

const char *get_task_state_str(int task_id) { return state_name(get_task_state(task_id)); }

const char *get_comm_state_str(void)
{
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);
    switch (snapshot.CommState) {
    case COMM_READY: return "ready";
    case COMM_SYNCING: return "syncing";
    default: return "offline";
    }
}

static bool task_state_is_active(TaskState_t state)
{
    return state == TASK_QUEUED || state == TASK_ASSIGNED || state == TASK_LOCKED;
}

/* Pure transition policy. Correlation is checked separately before this
 * function, so an old command can never revive a terminal mission. */
static transition_decision_t transition_policy(protocol_command_type_t command,
                                               TaskState_t current,
                                               bool call_pending)
{
    switch (command) {
    case PROTOCOL_CMD_ACCEPTED:
        if (current == TASK_QUEUED) return TRANSITION_NOOP;
        return current == TASK_IDLE && call_pending ? TRANSITION_APPLY : TRANSITION_REJECT;
    case PROTOCOL_CMD_ASSIGNED:
        if (current == TASK_ASSIGNED) return TRANSITION_NOOP;
        return current == TASK_QUEUED ? TRANSITION_APPLY : TRANSITION_REJECT;
    case PROTOCOL_CMD_LOCKED:
        if (current == TASK_LOCKED) return TRANSITION_NOOP;
        return current == TASK_QUEUED || current == TASK_ASSIGNED
                   ? TRANSITION_APPLY : TRANSITION_REJECT;
    case PROTOCOL_CMD_COMPLETED:
        if (current == TASK_COMPLETED) return TRANSITION_NOOP;
        return task_state_is_active(current) ? TRANSITION_APPLY : TRANSITION_REJECT;
    case PROTOCOL_CMD_OVERDUE:
        return task_state_is_active(current) ? TRANSITION_APPLY : TRANSITION_REJECT;
    default:
        return TRANSITION_REJECT;
    }
}

static void mission_set_state(int task_id, TaskState_t state, uint32_t timestamp)
{
    if (state == TASK_IDLE) {
        /* Reset mission/call/cancel liên quan trong một critical section để
         * heartbeat/output không quan sát tổ hợp trạng thái nửa cũ nửa mới. */
        status_reset_mission(task_id, timestamp);
        memset(s_agv_id[task_id - 1], 0, sizeof(s_agv_id[0]));
    } else {
        status_set_mission(task_id, state, timestamp);
    }
    ESP_LOGI(TAG, "Task %d -> %s", task_id, state_name(state));
}

static void request_call(int task_id, uint32_t timestamp)
{
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);
    const bool cancel_blocks_this_task = snapshot.Cancel.pending &&
                                         snapshot.CancelTarget == (uint8_t)task_id;
    if (snapshot.CommState != COMM_READY || !network_link_is_connected() ||
        !mqtt_is_connected() || cancel_blocks_this_task ||
        snapshot.Mission[task_id - 1] != TASK_IDLE ||
        snapshot.Call[task_id - 1].pending) {
        ESP_LOGI(TAG, "CALL task %d rejected by admission conditions", task_id);
        return;
    }
    uint32_t sequence;
    if (sequence_next(&sequence) != ESP_OK) return;
    const uint32_t now_ms = mission_now_ms();
    status_start_call(task_id, sequence, now_ms, timestamp);
    if (!mqtt_publish_call(task_id, sequence, timestamp)) {
        /* Adapter chưa nhận bản tin (queue/outbox đang bận): giữ transaction
         * cùng seq nhưng thử sớm, không tính đây là một lần retry đã gửi. */
        (void)status_schedule_call_retry_if_matches(
            task_id, sequence, now_ms + TRANSACTION_QUEUE_BUSY_RETRY_MS);
        ESP_LOGW(TAG, "CALL task %d seq=%lu waiting for MQTT TX capacity",
                 task_id, (unsigned long)sequence);
    }
    /* Trạng thái pending chỉ là cục bộ; Mission vẫn IDLE cho tới khi WCS
     * chấp nhận. */
    status_request_feedback(OUTPUT_FEEDBACK_CALL_REQUESTED);
    ESP_LOGI(TAG, "CALL task %d pending seq=%lu", task_id, (unsigned long)sequence);
}

static void request_cancel(uint32_t timestamp)
{
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);
    if (snapshot.CommState != COMM_READY || snapshot.Cancel.pending ||
        !network_link_is_connected() || !mqtt_is_connected()) return;
    int target = 0;
    for (int task = 1; task <= 2; ++task) {
        const TaskState_t task_state = snapshot.Mission[task - 1];
        const bool cancelable = task_state == TASK_QUEUED || task_state == TASK_ASSIGNED;
        if (cancelable &&
            (!target || snapshot.CallSequence[task - 1] >
                            snapshot.CallSequence[target - 1])) {
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
    const uint32_t now_ms = mission_now_ms();
    status_start_cancel(target, sequence, now_ms, timestamp);
    if (!mqtt_publish_cancel(target, sequence, timestamp)) {
        (void)status_schedule_cancel_retry_if_matches(
            target, sequence, now_ms + TRANSACTION_QUEUE_BUSY_RETRY_MS);
        ESP_LOGW(TAG, "CANCEL task %d seq=%lu waiting for MQTT TX capacity",
                 target, (unsigned long)sequence);
    }
    ESP_LOGI(TAG, "CANCEL task %d pending seq=%lu", target, (unsigned long)sequence);
}

void handle_button_press(int button_id, uint32_t timestamp)
{
    if (button_id == 1 || button_id == 2) request_call(button_id, timestamp);
    else if (button_id == 3) request_cancel(timestamp);
}

static bool command_matches_call(const callbox_status_t *snapshot,
                                 int task_id, uint32_t ref_seq)
{
    return snapshot != NULL && task_id >= 1 && task_id <= 2 && ref_seq != 0 &&
           snapshot->CallSequence[task_id - 1] == ref_seq;
}

static void handle_wcs_command(const protocol_command_t *cmd)
{
    const int task = cmd->task;
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);
    if (cmd->type == PROTOCOL_CMD_SYNC) {
        if (snapshot.CommState != COMM_SYNCING || cmd->ref_seq != s_sync_sequence) {
            ESP_LOGW(TAG, "Ignoring unmatched sync ref=%lu", (unsigned long)cmd->ref_seq);
            return;
        }
        for (int i = 0; i < 2; ++i) {
            if (task_state_is_active(cmd->sync_state[i]) && cmd->sync_call_seq[i] == 0) {
                ESP_LOGW(TAG, "Ignoring unsafe sync: active task %d has no call sequence",
                         i + 1);
                return;
            }
        }
        for (int i = 0; i < 2; ++i) {
            const int sync_task = i + 1;
            const TaskState_t sync_state = cmd->sync_state[i] == TASK_COMPLETED
                                               ? TASK_IDLE : cmd->sync_state[i];
            status_reconcile_mission(sync_task, sync_state,
                                     cmd->sync_call_seq[i], cmd->timestamp);
            strncpy(s_agv_id[i], cmd->sync_agv_id[i], sizeof(s_agv_id[i]) - 1);
            s_agv_id[i][sizeof(s_agv_id[i]) - 1] = '\0';
            ESP_LOGI(TAG, "Task %d -> %s (WCS sync)", sync_task,
                     state_name(sync_state));
        }
        status_clear_cancel();
        status_set_tower_warning(0, TOWER_WARNING_NONE);
        memset(&s_sync_transaction, 0, sizeof(s_sync_transaction));
        s_sync_sequence = 0;
        s_sync_waiting_for_reconnect = false;
        status_set_comm_state(COMM_READY);
        ESP_LOGI(TAG, "WCS sync accepted; Callbox ready");
        return;
    }
    if (snapshot.CommState != COMM_READY) {
        /* Lifecycle được ưu tiên hơn command trong queue. Command còn tồn từ
         * phiên cũ hoặc tới trước sync snapshot không được phép mutate mission. */
        ESP_LOGW(TAG, "Ignoring WCS cmd=%s while communication is %s",
                 protocol_command_name(cmd->type), get_comm_state_str());
        return;
    }
    if (task < 1 || task > 2) return;
    if (cmd->type == PROTOCOL_CMD_CANCEL_ACK) {
        if (!snapshot.Cancel.pending || snapshot.CancelTarget != task ||
            cmd->ref_seq != snapshot.Cancel.seq) {
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
        const bool rejects_call = snapshot.Call[task - 1].pending &&
                                  cmd->ref_seq == snapshot.Call[task - 1].seq;
        const bool rejects_cancel = snapshot.Cancel.pending &&
                                    snapshot.CancelTarget == task &&
                                    cmd->ref_seq == snapshot.Cancel.seq;
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

    if (!command_matches_call(&snapshot, task, cmd->ref_seq)) {
        ESP_LOGW(TAG, "Ignoring stale WCS cmd task=%d ref=%lu", task, (unsigned long)cmd->ref_seq);
        return;
    }
    const TaskState_t current = snapshot.Mission[task - 1];
    const transition_decision_t decision = transition_policy(
        cmd->type, current, snapshot.Call[task - 1].pending);
    if (decision == TRANSITION_NOOP) {
        ESP_LOGI(TAG, "Ignoring duplicate WCS cmd=%s task=%d state=%s",
                 protocol_command_name(cmd->type), task, state_name(current));
        return;
    }
    if (decision == TRANSITION_REJECT) {
        ESP_LOGW(TAG, "Blocked invalid WCS transition cmd=%s task=%d state=%s ref=%lu",
                 protocol_command_name(cmd->type), task, state_name(current),
                 (unsigned long)cmd->ref_seq);
        return;
    }
    if (cmd->type == PROTOCOL_CMD_ACCEPTED) {
        status_confirm_call(task, TASK_QUEUED, cmd->timestamp);
        status_clear_task_error(task);
        status_clear_tower_warning_for_task(task);
        ESP_LOGI(TAG, "Task %d -> %s", task, state_name(TASK_QUEUED));
    } else if (cmd->type == PROTOCOL_CMD_ASSIGNED) {
        status_confirm_call(task, TASK_ASSIGNED, cmd->timestamp);
        status_clear_task_error(task);
        status_clear_tower_warning_for_task(task);
        strncpy(s_agv_id[task - 1], cmd->agv_id, sizeof(s_agv_id[0]) - 1);
        s_agv_id[task - 1][sizeof(s_agv_id[0]) - 1] = '\0';
        ESP_LOGI(TAG, "Task %d -> %s", task, state_name(TASK_ASSIGNED));
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
    status_set_comm_state(COMM_SYNCING);
    const uint32_t now_ms = mission_now_ms();

    /* Một lần mất/kết nối lại transport không tạo giao dịch sync mới. Giữ cùng
     * seq để WCS có thể khử trùng lặp và tránh commit NVS theo mỗi lần retry. */
    if (s_sync_transaction.pending && s_sync_transaction.seq != 0U) {
        if (s_sync_waiting_for_reconnect) {
            if (mqtt_publish_sync_request(s_sync_transaction.seq,
                                          s_sync_transaction.timestamp)) {
                s_sync_transaction.retry_at_ms = now_ms + s_sync_retry_interval_ms;
                s_sync_waiting_for_reconnect = false;
            } else {
                s_sync_transaction.retry_at_ms = now_ms + SYNC_RETRY_QUEUE_BUSY_MS;
            }
        }
        ESP_LOGW(TAG, "WCS sync transaction remains seq=%lu after MQTT event",
                 (unsigned long)s_sync_transaction.seq);
        return;
    }

    uint32_t sequence;
    if (sequence_next(&sequence) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot allocate sync sequence");
        status_set_comm_state(COMM_OFFLINE);
        return;
    }
    s_sync_sequence = sequence;
    s_sync_transaction.pending = true;
    s_sync_transaction.seq = sequence;
    s_sync_transaction.retry_count = 0;
    s_sync_retry_interval_ms = SYNC_RETRY_INITIAL_MS;
    s_sync_waiting_for_reconnect = false;
    s_sync_transaction.retry_at_ms = now_ms + s_sync_retry_interval_ms;
    /* Sync không timeout thành giao dịch mới. WCS có thể hỏng nhiều giờ; Callbox
     * ở COMM_SYNCING an toàn và retry có backoff với chính seq này. */
    s_sync_transaction.deadline_ms = 0U;
    s_sync_transaction.timestamp = (uint32_t)time(NULL);
    if (!mqtt_publish_sync_request(sequence, s_sync_transaction.timestamp)) {
        s_sync_transaction.retry_at_ms = now_ms + SYNC_RETRY_QUEUE_BUSY_MS;
    }
    ESP_LOGI(TAG, "Requested WCS mission sync seq=%lu", (unsigned long)sequence);
}

static void tick_sync(void)
{
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);
    if (snapshot.CommState != COMM_SYNCING || !s_sync_transaction.pending) return;
    const uint32_t now_ms = mission_now_ms();
    if (!time_reached(now_ms, s_sync_transaction.retry_at_ms) || !mqtt_is_connected()) return;

    if (!mqtt_publish_sync_request(s_sync_transaction.seq,
                                   s_sync_transaction.timestamp)) {
        /* Queue/outbox đang bận: không quay nóng và không tăng exponential
         * backoff cho một bản tin chưa được adapter nhận. */
        s_sync_transaction.retry_at_ms = now_ms + SYNC_RETRY_QUEUE_BUSY_MS;
        return;
    }

    if (s_sync_transaction.retry_count < UINT8_MAX) s_sync_transaction.retry_count++;
    if (s_sync_retry_interval_ms < SYNC_RETRY_MAX_MS) {
        const uint32_t doubled = s_sync_retry_interval_ms * 2U;
        s_sync_retry_interval_ms = doubled < SYNC_RETRY_MAX_MS
                                       ? doubled : SYNC_RETRY_MAX_MS;
    }
    s_sync_transaction.retry_at_ms = now_ms + s_sync_retry_interval_ms;
    ESP_LOGW(TAG, "WCS silent; sync retry seq=%lu attempt=%u next=%lus",
             (unsigned long)s_sync_transaction.seq, s_sync_transaction.retry_count,
             (unsigned long)(s_sync_retry_interval_ms / 1000U));
}

static void publish_output_snapshot(void)
{
    callbox_status_t status_snapshot;
    status_get_snapshot(&status_snapshot);
    const app_output_snapshot_t snapshot = {
        .mission = { status_snapshot.Mission[0], status_snapshot.Mission[1] },
        .call_pending = { status_snapshot.Call[0].pending,
                          status_snapshot.Call[1].pending },
        .cancel_pending = status_snapshot.Cancel.pending,
        .comm_state = status_snapshot.CommState,
        .tower_warning = status_snapshot.TowerWarning,
        .task_error_until_ms = { status_snapshot.TaskErrorUntilMs[0],
                                 status_snapshot.TaskErrorUntilMs[1] },
        .cancel_ack_until_ms = status_snapshot.CancelAckUntilMs,
        .feedback = status_snapshot.Feedback,
        .feedback_generation = status_snapshot.FeedbackGeneration,
    };
    output_renderer_publish(&snapshot);
}

static bool tick_transactions(void)
{
    const uint32_t now_ms = mission_now_ms();
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);

    for (int task = 1; task <= 2; ++task) {
        const mission_transaction_t *const call = &snapshot.Call[task - 1];
        if (!call->pending) continue;

        if (time_reached(now_ms, call->deadline_ms)) {
            ESP_LOGW(TAG, "CALL task %d timed out seq=%lu after %u retries",
                     task, (unsigned long)call->seq, call->retry_count);
            if (status_timeout_call_if_matches(task, call->seq, 0U)) {
                memset(s_agv_id[task - 1], 0, sizeof(s_agv_id[0]));
                status_set_task_error(task, now_ms + 2000U);
                status_request_feedback(OUTPUT_FEEDBACK_TRANSACTION_FAILED);
                /* ACK có thể đã thất lạc sau khi WCS commit CALL. Không ở lại
                 * READY rồi cho phép một CALL mới dựa trên trạng thái local. */
                s_reconcile_after_transaction_timeout = true;
            }
        } else if (call->retry_count < MISSION_MAX_RETRIES &&
                   time_reached(now_ms, call->retry_at_ms) &&
                   network_link_is_connected() && mqtt_is_connected()) {
            /* Truyền lại cũng là một giao dịch: không bao giờ tiêu thụ seq
             * mới. */
            if (mqtt_publish_call(task, call->seq, call->timestamp)) {
                if (status_note_call_retry_if_matches(
                        task, call->seq, now_ms + MISSION_RETRY_INTERVAL_MS)) {
                    ESP_LOGW(TAG, "CALL retry task %d seq=%lu attempt=%u",
                             task, (unsigned long)call->seq,
                             (unsigned)(call->retry_count + 1U));
                }
            } else {
                (void)status_schedule_call_retry_if_matches(
                    task, call->seq, now_ms + TRANSACTION_QUEUE_BUSY_RETRY_MS);
            }
        }
    }

    const mission_transaction_t *const cancel = &snapshot.Cancel;
    if (!cancel->pending) return s_reconcile_after_transaction_timeout;
    if (time_reached(now_ms, cancel->deadline_ms)) {
        ESP_LOGW(TAG, "CANCEL task %u timed out seq=%lu after %u retries",
                 snapshot.CancelTarget, (unsigned long)cancel->seq, cancel->retry_count);
        if (status_clear_cancel_if_matches(snapshot.CancelTarget, cancel->seq)) {
            status_request_feedback(OUTPUT_FEEDBACK_TRANSACTION_FAILED);
            /* CANCEL cũng có thể đã được WCS commit nhưng ACK thất lạc. Khóa
             * admission và xin snapshot authoritative trước thao tác kế tiếp. */
            s_reconcile_after_transaction_timeout = true;
        }
    } else if (cancel->retry_count < MISSION_MAX_RETRIES &&
               time_reached(now_ms, cancel->retry_at_ms) &&
               network_link_is_connected() && mqtt_is_connected()) {
        if (mqtt_publish_cancel(snapshot.CancelTarget, cancel->seq,
                                cancel->timestamp)) {
            if (status_note_cancel_retry_if_matches(
                    snapshot.CancelTarget, cancel->seq,
                    now_ms + MISSION_RETRY_INTERVAL_MS)) {
                ESP_LOGW(TAG, "CANCEL retry task %u seq=%lu attempt=%u",
                         snapshot.CancelTarget, (unsigned long)cancel->seq,
                         (unsigned)(cancel->retry_count + 1U));
            }
        } else {
            (void)status_schedule_cancel_retry_if_matches(
                snapshot.CancelTarget, cancel->seq,
                now_ms + TRANSACTION_QUEUE_BUSY_RETRY_MS);
        }
    }
    return s_reconcile_after_transaction_timeout;
}

void state_machine_init(void)
{
    status_init();
    button_gate_reset();
    status_set_comm_state(COMM_OFFLINE);
    s_cancel_hold_active = false;
    s_cancel_hold_consumed = false;
    s_sync_retry_interval_ms = SYNC_RETRY_INITIAL_MS;
    s_sync_waiting_for_reconnect = false;
    s_reconcile_after_transaction_timeout = false;
    s_active_session_epoch = 0U;
    ESP_LOGI(TAG, "Mission Manager initialized (single business-state owner)");
}

static void handle_button_event(const ButtonMsg_t *button)
{
    const bool accepted_press = button_gate_accept(button);

    if (button->button_id != 3) {
        if (accepted_press) {
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
    callbox_status_t snapshot;
    status_get_snapshot(&snapshot);
    if (!s_cancel_hold_active || s_cancel_hold_consumed || !snapshot.Button[2].level ||
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
    /* Phản hồi network (GPIO46) do composition root đăng ký qua callback
     * wifi_set_rescue_ap_changed_callback — Mission không gọi network_status
     * trực tiếp. */
}

void state_machine_task(void *arg)
{
    (void)arg;
    ButtonMsg_t button;
    app_event_t event;
    QueueHandle_t button_queue = io_handler_get_button_event_queue();
    health_monitor_check_in(HEALTH_TASK_STATE_MACHINE);
    while (true) {
        for (uint32_t count = 0; count < STATE_MAX_BUTTON_EVENTS_PER_CYCLE &&
                                 xQueueReceive(button_queue, &button, 0) == pdTRUE;
             ++count) {
            handle_button_event(&button);
        }
        for (uint32_t count = 0; count < STATE_MAX_APP_EVENTS_PER_CYCLE &&
                                 app_event_receive(&event, 0);
             ++count) {
            if (event.type == APP_EVENT_WCS_COMMAND) {
                if (event.session_epoch != s_active_session_epoch ||
                    event.session_epoch == 0U) {
                    ESP_LOGW(TAG, "Ignoring stale WCS command epoch=%lu active=%lu",
                             (unsigned long)event.session_epoch,
                             (unsigned long)s_active_session_epoch);
                } else {
                    handle_wcs_command(&event.command);
                }
            } else if (event.type == APP_EVENT_MQTT_CONNECTED) {
                s_active_session_epoch = event.session_epoch;
                /* Không purge mù tại đây: broker có thể gửi sync reply của
                 * chính phiên mới ngay sau SUBACK, trước khi Mission Manager
                 * đọc lifecycle latch. Epoch filter bên trên sẽ tự loại command
                 * phiên cũ mà vẫn giữ command hợp lệ của phiên hiện tại. */
                begin_wcs_sync();
            } else if (event.type == APP_EVENT_MQTT_DISCONNECTED) {
                (void)app_event_queue_purge_commands();
                /* Giữ sync transaction/seq trong RAM qua reconnect. Không cấp
                 * sequence mới và không ghi NVS chỉ vì transport chập chờn. */
                if (s_sync_transaction.pending) s_sync_waiting_for_reconnect = true;
                status_set_comm_state(COMM_OFFLINE);
                ESP_LOGW(TAG, "MQTT offline; button admission paused until WCS sync");
            } else if (event.type == APP_EVENT_WCS_RESYNC_REQUIRED) {
                if (event.session_epoch != s_active_session_epoch ||
                    event.session_epoch == 0U) {
                    ESP_LOGW(TAG, "Ignoring stale resync epoch=%lu active=%lu",
                             (unsigned long)event.session_epoch,
                             (unsigned long)s_active_session_epoch);
                    continue;
                }
                const uint32_t purged = app_event_queue_purge_commands();
                /* Nếu sync đang pending, buộc gửi lại ngay cùng seq thay vì
                 * chờ hết backoff 300 giây; nếu READY, tạo một sync mới. */
                if (s_sync_transaction.pending) s_sync_waiting_for_reconnect = true;
                ESP_LOGE(TAG, "WCS command loss detected; purged %lu and resyncing",
                         (unsigned long)purged);
                begin_wcs_sync();
            }
        }
        if (tick_transactions()) {
            s_reconcile_after_transaction_timeout = false;
            begin_wcs_sync();
        }
        tick_sync();
        tick_cancel_rescue_hold();
        publish_output_snapshot();
        health_monitor_check_in(HEALTH_TASK_STATE_MACHINE);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
