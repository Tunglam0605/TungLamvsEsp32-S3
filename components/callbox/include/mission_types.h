/**
 * @file mission_types.h
 * @brief Các kiểu miền mission dùng chung giữa Mission Manager và snapshot.
 */
#ifndef CALLBOX_MISSION_TYPES_H
#define CALLBOX_MISSION_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TASK_IDLE = 0,
    TASK_QUEUED,
    TASK_ASSIGNED,
    TASK_LOCKED,
    TASK_COMPLETED,
} TaskState_t;

/* Mission Manager là người ghi duy nhất. Các lần publish lặp lại giữ nguyên
 * seq để WCS khử trùng lặp một giao dịch call hoặc cancel logic. */
typedef struct {
    bool pending;
    uint32_t seq;
    uint8_t retry_count;
    uint32_t retry_at_ms;
    uint32_t deadline_ms;
    uint32_t timestamp;
} mission_transaction_t;

/* Callbox phải hoàn tất việc đồng bộ trạng thái với WCS trước khi chấp
 * nhận các hành động call/cancel cục bộ sau khi kết nối (hoặc kết nối lại)
 * MQTT. */
typedef enum {
    COMM_OFFLINE = 0,
    COMM_SYNCING,
    COMM_READY,
} comm_state_t;

/* Cảnh báo WCS là trạng thái ứng dụng. Output Renderer đọc nó từ snapshot
 * và không bao giờ tự thay đổi cảnh báo. */
typedef enum {
    TOWER_WARNING_NONE = 0,
    TOWER_WARNING_OVERDUE,
    TOWER_WARNING_ERROR,
} tower_warning_t;

/* Yêu cầu phản hồi ứng dụng ngắn hạn. Nó là dữ liệu trong snapshot; chỉ
 * Output Renderer chuyển nó thành mẫu bíp vật lý. */
typedef enum {
    OUTPUT_FEEDBACK_NONE = 0,
    OUTPUT_FEEDBACK_CALL_REQUESTED,
    OUTPUT_FEEDBACK_TASK_ASSIGNED,
    OUTPUT_FEEDBACK_CONFIG_SAVED,
    OUTPUT_FEEDBACK_CANCEL_ACKNOWLEDGED,
    OUTPUT_FEEDBACK_TRANSACTION_FAILED,
    OUTPUT_FEEDBACK_OTA_STAGED,
    OUTPUT_FEEDBACK_OTA_INSTALLING,
    OUTPUT_FEEDBACK_OTA_FAILED,
} output_feedback_t;

#define MISSION_RETRY_INTERVAL_MS 5000U
#define MISSION_MAX_RETRIES       2U
#define MISSION_TRANSACTION_TIMEOUT_MS \
    ((MISSION_MAX_RETRIES + 1U) * MISSION_RETRY_INTERVAL_MS)
#define TASK_REJECT_FLASH_WINDOW_MS 1000U
#define CANCEL_ACK_FLASH_WINDOW_MS  700U

#endif /* CALLBOX_MISSION_TYPES_H */
