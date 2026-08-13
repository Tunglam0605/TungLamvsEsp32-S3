/**
 * @file    status.c
 * @brief   Cập nhật snapshot trạng thái liên tầng.
 */
#include "status.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

volatile callbox_status_t status;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

#define STATUS_LOCK()   portENTER_CRITICAL(&s_status_lock)
#define STATUS_UNLOCK() portEXIT_CRITICAL(&s_status_lock)

void status_init(void)
{
    STATUS_LOCK();
    memset((void *)&status, 0, sizeof(status));
    for (int i = 0; i < STATUS_MISSION_COUNT; ++i) {
        status.Mission[i] = TASK_IDLE;
    }
    STATUS_UNLOCK();
}

void status_get_snapshot(callbox_status_t *snapshot)
{
    if (snapshot == NULL) return;
    STATUS_LOCK();
    memcpy(snapshot, (const void *)&status, sizeof(*snapshot));
    STATUS_UNLOCK();
}

void status_set_inputs(uint8_t active_mask)
{
    STATUS_LOCK();
    for (int i = 0; i < STATUS_INPUT_COUNT; ++i) {
        status.IN[i] = (active_mask & (1u << i)) != 0;
    }
    STATUS_UNLOCK();
}

void status_set_button(int button_id, bool level, bool pressed_edge)
{
    if (button_id < 1 || button_id > STATUS_BUTTON_COUNT) return;
    STATUS_LOCK();
    status.Button[button_id - 1].level = level;
    status.Button[button_id - 1].pressed_edge = pressed_edge;
    STATUS_UNLOCK();
}

void status_set_mission(int task_id, TaskState_t state_value, uint32_t timestamp)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    STATUS_LOCK();
    status.Mission[task_id - 1] = state_value;
    status.MissionTimestamp[task_id - 1] = timestamp;
    STATUS_UNLOCK();
}

static void transaction_start(mission_transaction_t *transaction, uint32_t sequence,
                              uint32_t now_ms, uint32_t timestamp)
{
    transaction->pending = true;
    transaction->seq = sequence;
    transaction->retry_count = 0;
    transaction->retry_at_ms = now_ms + MISSION_RETRY_INTERVAL_MS;
    transaction->deadline_ms = now_ms + MISSION_TRANSACTION_TIMEOUT_MS;
    transaction->timestamp = timestamp;
}

void status_start_call(int task_id, uint32_t sequence, uint32_t now_ms, uint32_t timestamp)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    STATUS_LOCK();
    transaction_start((mission_transaction_t *)&status.Call[task_id - 1], sequence, now_ms, timestamp);
    status.CallSequence[task_id - 1] = sequence;
    STATUS_UNLOCK();
}

void status_clear_call(int task_id)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    STATUS_LOCK();
    memset((void *)&status.Call[task_id - 1], 0, sizeof(status.Call[0]));
    STATUS_UNLOCK();
}

void status_note_call_retry(int task_id, uint32_t next_retry_ms)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    STATUS_LOCK();
    status.Call[task_id - 1].retry_count++;
    status.Call[task_id - 1].retry_at_ms = next_retry_ms;
    STATUS_UNLOCK();
}

bool status_schedule_call_retry_if_matches(int task_id, uint32_t sequence,
                                           uint32_t next_retry_ms)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT || sequence == 0U) return false;
    bool matched = false;
    STATUS_LOCK();
    mission_transaction_t *const call =
        (mission_transaction_t *)&status.Call[task_id - 1];
    if (call->pending && call->seq == sequence) {
        call->retry_at_ms = next_retry_ms;
        matched = true;
    }
    STATUS_UNLOCK();
    return matched;
}

bool status_note_call_retry_if_matches(int task_id, uint32_t sequence,
                                       uint32_t next_retry_ms)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT || sequence == 0U) return false;
    bool matched = false;
    STATUS_LOCK();
    mission_transaction_t *const call =
        (mission_transaction_t *)&status.Call[task_id - 1];
    if (call->pending && call->seq == sequence) {
        if (call->retry_count < UINT8_MAX) call->retry_count++;
        call->retry_at_ms = next_retry_ms;
        matched = true;
    }
    STATUS_UNLOCK();
    return matched;
}

bool status_timeout_call_if_matches(int task_id, uint32_t sequence,
                                    uint32_t timestamp)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT || sequence == 0U) return false;
    const int index = task_id - 1;
    bool matched = false;
    STATUS_LOCK();
    if (status.Call[index].pending && status.Call[index].seq == sequence) {
        memset((void *)&status.Call[index], 0, sizeof(status.Call[index]));
        status.CallSequence[index] = 0U;
        status.Mission[index] = TASK_IDLE;
        status.MissionTimestamp[index] = timestamp;
        if (status.Cancel.pending && status.CancelTarget == (uint8_t)task_id) {
            memset((void *)&status.Cancel, 0, sizeof(status.Cancel));
            status.CancelTarget = 0U;
        }
        matched = true;
    }
    STATUS_UNLOCK();
    return matched;
}

void status_start_cancel(int target, uint32_t sequence, uint32_t now_ms, uint32_t timestamp)
{
    STATUS_LOCK();
    transaction_start((mission_transaction_t *)&status.Cancel, sequence, now_ms, timestamp);
    status.CancelTarget = (uint8_t)target;
    STATUS_UNLOCK();
}

void status_clear_cancel(void)
{
    STATUS_LOCK();
    memset((void *)&status.Cancel, 0, sizeof(status.Cancel));
    status.CancelTarget = 0;
    STATUS_UNLOCK();
}

void status_note_cancel_retry(uint32_t next_retry_ms)
{
    STATUS_LOCK();
    status.Cancel.retry_count++;
    status.Cancel.retry_at_ms = next_retry_ms;
    STATUS_UNLOCK();
}

bool status_schedule_cancel_retry_if_matches(int target, uint32_t sequence,
                                             uint32_t next_retry_ms)
{
    if (target < 1 || target > STATUS_MISSION_COUNT || sequence == 0U) return false;
    bool matched = false;
    STATUS_LOCK();
    if (status.Cancel.pending && status.CancelTarget == (uint8_t)target &&
        status.Cancel.seq == sequence) {
        status.Cancel.retry_at_ms = next_retry_ms;
        matched = true;
    }
    STATUS_UNLOCK();
    return matched;
}

bool status_note_cancel_retry_if_matches(int target, uint32_t sequence,
                                         uint32_t next_retry_ms)
{
    if (target < 1 || target > STATUS_MISSION_COUNT || sequence == 0U) return false;
    bool matched = false;
    STATUS_LOCK();
    if (status.Cancel.pending && status.CancelTarget == (uint8_t)target &&
        status.Cancel.seq == sequence) {
        if (status.Cancel.retry_count < UINT8_MAX) status.Cancel.retry_count++;
        status.Cancel.retry_at_ms = next_retry_ms;
        matched = true;
    }
    STATUS_UNLOCK();
    return matched;
}

bool status_clear_cancel_if_matches(int target, uint32_t sequence)
{
    if (target < 1 || target > STATUS_MISSION_COUNT || sequence == 0U) return false;
    bool matched = false;
    STATUS_LOCK();
    if (status.Cancel.pending && status.CancelTarget == (uint8_t)target &&
        status.Cancel.seq == sequence) {
        memset((void *)&status.Cancel, 0, sizeof(status.Cancel));
        status.CancelTarget = 0U;
        matched = true;
    }
    STATUS_UNLOCK();
    return matched;
}

void status_set_comm_state(comm_state_t state)
{
    STATUS_LOCK();
    status.CommState = state;
    STATUS_UNLOCK();
}

void status_set_tower_warning(int task_id, tower_warning_t warning)
{
    STATUS_LOCK();
    status.TowerWarning = warning;
    status.TowerWarningTask = warning == TOWER_WARNING_NONE ? 0U : (uint8_t)task_id;
    STATUS_UNLOCK();
}

void status_clear_tower_warning_for_task(int task_id)
{
    STATUS_LOCK();
    if (status.TowerWarningTask == (uint8_t)task_id) {
        status.TowerWarning = TOWER_WARNING_NONE;
        status.TowerWarningTask = 0U;
    }
    STATUS_UNLOCK();
}

void status_set_task_error(int task_id, uint32_t until_ms)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    STATUS_LOCK();
    status.TaskErrorUntilMs[task_id - 1] = until_ms;
    STATUS_UNLOCK();
}

void status_clear_task_error(int task_id)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    STATUS_LOCK();
    status.TaskErrorUntilMs[task_id - 1] = 0;
    STATUS_UNLOCK();
}

void status_set_cancel_ack_feedback(uint32_t until_ms)
{
    STATUS_LOCK();
    status.CancelAckUntilMs = until_ms;
    STATUS_UNLOCK();
}

void status_request_feedback(output_feedback_t feedback)
{
    STATUS_LOCK();
    status.Feedback = feedback;
    status.FeedbackGeneration++;
    STATUS_UNLOCK();
}

void status_set_call_sequence(int task_id, uint32_t sequence)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    STATUS_LOCK();
    status.CallSequence[task_id - 1] = sequence;
    STATUS_UNLOCK();
}

void status_confirm_call(int task_id, TaskState_t state_value, uint32_t timestamp)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    const int index = task_id - 1;
    STATUS_LOCK();
    memset((void *)&status.Call[index], 0, sizeof(status.Call[index]));
    status.Mission[index] = state_value;
    status.MissionTimestamp[index] = timestamp;
    STATUS_UNLOCK();
}

void status_reset_mission(int task_id, uint32_t timestamp)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    const int index = task_id - 1;
    STATUS_LOCK();
    memset((void *)&status.Call[index], 0, sizeof(status.Call[index]));
    status.CallSequence[index] = 0U;
    status.Mission[index] = TASK_IDLE;
    status.MissionTimestamp[index] = timestamp;
    if (status.Cancel.pending && status.CancelTarget == (uint8_t)task_id) {
        memset((void *)&status.Cancel, 0, sizeof(status.Cancel));
        status.CancelTarget = 0U;
    }
    STATUS_UNLOCK();
}

void status_reconcile_mission(int task_id, TaskState_t state_value,
                              uint32_t call_sequence, uint32_t timestamp)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    const int index = task_id - 1;
    STATUS_LOCK();
    memset((void *)&status.Call[index], 0, sizeof(status.Call[index]));
    status.CallSequence[index] = call_sequence;
    status.Mission[index] = state_value;
    status.MissionTimestamp[index] = timestamp;
    STATUS_UNLOCK();
}
