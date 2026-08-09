/**
 * @file    button_gate.h
 * @brief   Lớp trung gian khóa cạnh nhấn của nút.
 *
 *          IO handler chịu trách nhiệm đọc và debounce.  Module này chỉ
 *          chịu trách nhiệm biến chuỗi PRESSED/RELEASED thành đúng một
 *          sự kiện nhấn cho mỗi chu kỳ nhấn-thả, không biết nghiệp vụ task.
 */
#ifndef CALLBOX_BUTTON_GATE_H
#define CALLBOX_BUTTON_GATE_H

#include <stdbool.h>
#include "app_event.h"

/**
 * @brief Consume a debounced button transition.
 * @return true only for the first PRESSED edge after a RELEASED edge.
 */
bool button_gate_accept(const ButtonMsg_t *event);

/** @brief Xóa mọi latch nút bấm (dùng sau khi reset có kiểm soát). */
void button_gate_reset(void);

#endif /* CALLBOX_BUTTON_GATE_H */
