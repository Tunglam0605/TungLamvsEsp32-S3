/**
 * @file    status.h
 * @brief   Snapshot trạng thái dùng chung giữa BSP adapter và application.
 *
 *          BSP chỉ cập nhật IN.  BUTTON cập nhật Button.  TASK/state machine
 *          cập nhật Mission và CancelPending.  Không lớp nào đọc GPIO trực
 *          tiếp hoặc tự tạo một bản sao trạng thái nghiệp vụ khác.
 */
#ifndef CALLBOX_STATUS_H
#define CALLBOX_STATUS_H

#include <stdbool.h>
#include <stdint.h>
#include "mission_types.h"

#define STATUS_INPUT_COUNT   8
#define STATUS_BUTTON_COUNT  3
#define STATUS_MISSION_COUNT 2

typedef struct {
    bool level;        /* Stable logical level: true = pressed. */
    bool pressed_edge; /* One-cycle event set only by button_gate. */
} status_button_t;

typedef struct {
    bool IN[STATUS_INPUT_COUNT];
    status_button_t Button[STATUS_BUTTON_COUNT];
    TaskState_t Mission[STATUS_MISSION_COUNT];
    mission_transaction_t Call[STATUS_MISSION_COUNT];
    uint32_t CallSequence[STATUS_MISSION_COUNT];
    mission_transaction_t Cancel;
    uint8_t CancelTarget;
    comm_state_t CommState;
    tower_warning_t TowerWarning;
    uint8_t TowerWarningTask;
    uint32_t TaskErrorUntilMs[STATUS_MISSION_COUNT];
    uint32_t CancelAckUntilMs;
    uint32_t FeedbackGeneration;
    output_feedback_t Feedback;
    uint32_t MissionTimestamp[STATUS_MISSION_COUNT];
} callbox_status_t;

/* Field names intentionally follow the plant-control terminology. */
extern volatile callbox_status_t status;

void status_init(void);
void status_set_inputs(uint8_t active_mask);
void status_set_button(int button_id, bool level, bool pressed_edge);
void status_set_mission(int task_id, TaskState_t state, uint32_t timestamp);
void status_start_call(int task_id, uint32_t sequence, uint32_t now_ms, uint32_t timestamp);
void status_clear_call(int task_id);
void status_note_call_retry(int task_id, uint32_t next_retry_ms);
void status_start_cancel(int target, uint32_t sequence, uint32_t now_ms, uint32_t timestamp);
void status_clear_cancel(void);
void status_note_cancel_retry(uint32_t next_retry_ms);
void status_set_comm_state(comm_state_t state);
void status_set_tower_warning(int task_id, tower_warning_t warning);
void status_clear_tower_warning_for_task(int task_id);
void status_set_task_error(int task_id, uint32_t until_ms);
void status_clear_task_error(int task_id);
void status_set_cancel_ack_feedback(uint32_t until_ms);
void status_request_feedback(output_feedback_t feedback);

#endif /* CALLBOX_STATUS_H */
