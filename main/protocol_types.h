/**
 * @file protocol_types.h
 * @brief MQTT application-protocol types, independent of transport and Mission.
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

/* Rejection semantics are part of the WCS contract, not free-form UI text.
 * Unknown strings parse as NONE and are still logged safely as unspecified. */
typedef enum {
    REJECT_REASON_NONE = 0,
    REJECT_REASON_LOCKED,
    REJECT_REASON_DUPLICATE,
    REJECT_REASON_NO_TASK,
    REJECT_REASON_WCS_BUSY,
} reject_reason_t;

typedef struct {
    protocol_command_type_t type;
    int task;
    uint32_t ref_seq;
    uint32_t timestamp;
    char agv_id[32];
    /* Optional structured WCS rejection diagnostic. */
    reject_reason_t reason;
    /* Valid only for type=sync. WCS supplies both authoritative snapshots. */
    TaskState_t sync_state[2];
    uint32_t sync_call_seq[2];
    char sync_agv_id[2][32];
} protocol_command_t;

const char *protocol_command_name(protocol_command_type_t type);
const char *protocol_reject_reason_name(reject_reason_t reason);
bool protocol_parse_command_json(const char *payload, protocol_command_t *command);

#endif /* CALLBOX_PROTOCOL_TYPES_H */
