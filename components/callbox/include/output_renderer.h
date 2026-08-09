/**
 * @file output_renderer.h
 * @brief Bộ kết xuất chỉ đọc snapshot (immutable) cho các đầu ra
 *        hiển thị/âm thanh của Callbox.
 */
#ifndef CALLBOX_OUTPUT_RENDERER_H
#define CALLBOX_OUTPUT_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include "mission_types.h"

typedef struct {
    TaskState_t mission[2];
    bool call_pending[2];
    bool cancel_pending;
    comm_state_t comm_state;
    tower_warning_t tower_warning;
    uint32_t task_error_until_ms[2];
    uint32_t cancel_ack_until_ms;
    output_feedback_t feedback;
    uint32_t feedback_generation;
} app_output_snapshot_t;

void output_renderer_init(void);
void output_renderer_publish(const app_output_snapshot_t *snapshot);

#endif /* CALLBOX_OUTPUT_RENDERER_H */
