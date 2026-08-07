/**
 * @file    button_gate.c
 * @brief   Khóa cạnh nhấn/thả độc lập với logic nhiệm vụ.
 */
#include "button_gate.h"
#include "status.h"
#include <stddef.h>

static bool s_pressed_latched[3];

bool button_gate_accept(const ButtonMsg_t *event)
{
    if (event == NULL || event->button_id < 1 || event->button_id > 3) {
        return false;
    }

    const int index = event->button_id - 1;
    if (event->state == BTN_RELEASED) {
        /* Chỉ sau khi thấy RELEASED mới cho phép cạnh PRESSED tiếp theo. */
        s_pressed_latched[index] = false;
        status_set_button(event->button_id, false, false);
        return false;
    }

    if (event->state != BTN_PRESSED || s_pressed_latched[index]) {
        if (event->state == BTN_PRESSED) {
            status_set_button(event->button_id, true, false);
        }
        return false;
    }

    s_pressed_latched[index] = true;
    status_set_button(event->button_id, true, true);
    return true;
}

void button_gate_reset(void)
{
    s_pressed_latched[0] = false;
    s_pressed_latched[1] = false;
    s_pressed_latched[2] = false;
    for (int i = 1; i <= STATUS_BUTTON_COUNT; ++i) {
        status_set_button(i, false, false);
    }
}
