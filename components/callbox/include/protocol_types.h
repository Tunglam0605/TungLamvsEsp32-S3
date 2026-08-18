/**
 * @file protocol_types.h
 * @brief Các kiểu giao thức ứng dụng MQTT, độc lập với tầng truyền tải
 *        và Mission.
 */
#ifndef CALLBOX_PROTOCOL_TYPES_H
#define CALLBOX_PROTOCOL_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include "mission_types.h"

typedef enum {
    PROTOCOL_CMD_INVALID = 0,
    PROTOCOL_CMD_ACCEPTED,
    PROTOCOL_CMD_ASSIGNED,
    PROTOCOL_CMD_LOCKED,
    PROTOCOL_CMD_COMPLETED,
    PROTOCOL_CMD_CANCEL_ACK,
    PROTOCOL_CMD_REJECTED,
    PROTOCOL_CMD_OVERDUE,
    PROTOCOL_CMD_SYNC,
    PROTOCOL_CMD_CONFIG,
} protocol_command_type_t;

/* Ngữ nghĩa từ chối (rejection) là một phần của hợp đồng WCS, không phải
 * văn bản UI tự do. Chuỗi không xác định được phân tích thành NONE và vẫn
 * được ghi log an toàn như giá trị không chỉ định. */
typedef enum {
    REJECT_REASON_NONE = 0,
    REJECT_REASON_LOCKED,
    REJECT_REASON_DUPLICATE,
    REJECT_REASON_NO_TASK,
    REJECT_REASON_WCS_BUSY,
} reject_reason_t;

/* WCS may provide a human-readable terminal failure from its transport-order
 * engine.  Keep the original text for diagnostic logs; the enum above remains
 * only a convenience for the short legacy reasons. */
#define PROTOCOL_REJECT_REASON_TEXT_MAX 128U
#define PROTOCOL_ORDER_NAME_MAX          96U

typedef struct {
    protocol_command_type_t type;
    int task;
    uint32_t ref_seq;
    uint32_t timestamp;
    char agv_id[32];
    /* Chẩn đoán từ chối có cấu trúc (tùy chọn) từ WCS. */
    reject_reason_t reason;
    char reason_text[PROTOCOL_REJECT_REASON_TEXT_MAX];
    char order_name[PROTOCOL_ORDER_NAME_MAX];
    /* Chỉ hợp lệ với type=sync. WCS cung cấp cả hai snapshot có thẩm quyền. */
    TaskState_t sync_state[2];
    uint32_t sync_call_seq[2];
    char sync_agv_id[2][32];
} protocol_command_t;

const char *protocol_command_name(protocol_command_type_t type);
const char *protocol_reject_reason_name(reject_reason_t reason);
bool protocol_parse_command_json(const char *payload, protocol_command_t *command);

#endif /* CALLBOX_PROTOCOL_TYPES_H */
