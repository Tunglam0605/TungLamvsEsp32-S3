/**
 * @file mission_types.h
 * @brief Mission-domain types shared by the Mission Manager and its snapshots.
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

/* The Mission Manager is the sole writer. Repeated publishes keep seq so WCS
 * can deduplicate a single logical call or cancel transaction. */
typedef struct {
    bool pending;
    uint32_t seq;
    uint8_t retry_count;
    uint32_t retry_at_ms;
    uint32_t deadline_ms;
    uint32_t timestamp;
} mission_transaction_t;

/* The Callbox must finish a WCS state reconciliation before accepting local
 * call/cancel actions after an MQTT connection (or reconnection). */
typedef enum {
    COMM_OFFLINE = 0,
    COMM_SYNCING,
    COMM_READY,
} comm_state_t;

/* A WCS warning is application state.  Output Renderer reads it from a
 * snapshot and never changes the warning itself. */
typedef enum {
    TOWER_WARNING_NONE = 0,
    TOWER_WARNING_OVERDUE,
    TOWER_WARNING_ERROR,
} tower_warning_t;

/* A short-lived application feedback request. It is data in the snapshot;
 * only Output Renderer translates it to a physical buzzer pattern. */
typedef enum {
    OUTPUT_FEEDBACK_NONE = 0,
    OUTPUT_FEEDBACK_CALL_REQUESTED,
    OUTPUT_FEEDBACK_TASK_ASSIGNED,
    OUTPUT_FEEDBACK_CONFIG_SAVED,
    OUTPUT_FEEDBACK_CANCEL_ACKNOWLEDGED,
    OUTPUT_FEEDBACK_TRANSACTION_FAILED,
} output_feedback_t;

#define MISSION_RETRY_INTERVAL_MS 5000U
#define MISSION_MAX_RETRIES       2U
#define MISSION_TRANSACTION_TIMEOUT_MS \
    ((MISSION_MAX_RETRIES + 1U) * MISSION_RETRY_INTERVAL_MS)
#define TASK_REJECT_FLASH_WINDOW_MS 1000U
#define CANCEL_ACK_FLASH_WINDOW_MS  700U

#endif /* CALLBOX_MISSION_TYPES_H */
