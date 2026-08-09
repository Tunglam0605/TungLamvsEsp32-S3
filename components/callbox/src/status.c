/**
 * @file    status.c
 * @brief   Cập nhật snapshot trạng thái liên tầng.
 */
#include "status.h"
#include <string.h>

volatile callbox_status_t status;

void status_init(void)
{
    memset((void *)&status, 0, sizeof(status));
    for (int i = 0; i < STATUS_MISSION_COUNT; ++i) {
        status.Mission[i] = TASK_IDLE;
    }
}

void status_set_inputs(uint8_t active_mask)
{
    for (int i = 0; i < STATUS_INPUT_COUNT; ++i) {
        status.IN[i] = (active_mask & (1u << i)) != 0;
    }
}

void status_set_button(int button_id, bool level, bool pressed_edge)
{
    if (button_id < 1 || button_id > STATUS_BUTTON_COUNT) return;
    status.Button[button_id - 1].level = level;
    status.Button[button_id - 1].pressed_edge = pressed_edge;
}

void status_set_mission(int task_id, TaskState_t state_value, uint32_t timestamp)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    status.Mission[task_id - 1] = state_value;
    status.MissionTimestamp[task_id - 1] = timestamp;
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
    transaction_start((mission_transaction_t *)&status.Call[task_id - 1], sequence, now_ms, timestamp);
    status.CallSequence[task_id - 1] = sequence;
}

void status_clear_call(int task_id)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    memset((void *)&status.Call[task_id - 1], 0, sizeof(status.Call[0]));
}

void status_note_call_retry(int task_id, uint32_t next_retry_ms)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    status.Call[task_id - 1].retry_count++;
    status.Call[task_id - 1].retry_at_ms = next_retry_ms;
}

void status_start_cancel(int target, uint32_t sequence, uint32_t now_ms, uint32_t timestamp)
{
    transaction_start((mission_transaction_t *)&status.Cancel, sequence, now_ms, timestamp);
    status.CancelTarget = (uint8_t)target;
}

void status_clear_cancel(void)
{
    memset((void *)&status.Cancel, 0, sizeof(status.Cancel));
    status.CancelTarget = 0;
}

void status_note_cancel_retry(uint32_t next_retry_ms)
{
    status.Cancel.retry_count++;
    status.Cancel.retry_at_ms = next_retry_ms;
}

void status_set_comm_state(comm_state_t state)
{
    status.CommState = state;
}

void status_set_tower_warning(int task_id, tower_warning_t warning)
{
    status.TowerWarning = warning;
    status.TowerWarningTask = warning == TOWER_WARNING_NONE ? 0U : (uint8_t)task_id;
}

void status_clear_tower_warning_for_task(int task_id)
{
    if (status.TowerWarningTask == (uint8_t)task_id) {
        status.TowerWarning = TOWER_WARNING_NONE;
        status.TowerWarningTask = 0U;
    }
}

void status_set_task_error(int task_id, uint32_t until_ms)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    status.TaskErrorUntilMs[task_id - 1] = until_ms;
}

void status_clear_task_error(int task_id)
{
    if (task_id < 1 || task_id > STATUS_MISSION_COUNT) return;
    status.TaskErrorUntilMs[task_id - 1] = 0;
}

void status_set_cancel_ack_feedback(uint32_t until_ms)
{
    status.CancelAckUntilMs = until_ms;
}

void status_request_feedback(output_feedback_t feedback)
{
    status.Feedback = feedback;
    status.FeedbackGeneration++;
}
