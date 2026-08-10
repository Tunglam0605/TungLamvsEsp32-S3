/**
 * @file state_machine.h
 * @brief Giao diện công khai của Mission Manager.
 *
 * Đầu vào từ nút bấm và MQTT không bao giờ biến đổi mission trực tiếp.
 * Mission Manager sở hữu các chuyển đổi mission và các giao dịch
 * gọi/hủy (call/cancel) đi ra ngoài.
 */
#ifndef CALLBOX_STATE_MACHINE_H
#define CALLBOX_STATE_MACHINE_H

#include <stdint.h>
#include "mission_types.h"

void state_machine_init(void);
void state_machine_task(void *pvParameters);

/* Nút 1/2 yêu cầu gọi (call); nút 3 yêu cầu hủy mission mới nhất có thể
 * hủy. Việc tiếp nhận (admission) và thử lại do manager sở hữu. */
void handle_button_press(int button_id, uint32_t timestamp);

TaskState_t get_task_state(int task_id);
const char *get_task_state_str(int task_id);
const char *get_comm_state_str(void);

#endif /* CALLBOX_STATE_MACHINE_H */
