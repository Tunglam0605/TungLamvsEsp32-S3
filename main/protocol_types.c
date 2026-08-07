/**
 * @file protocol_types.c
 * @brief Strict JSON codec for the Callbox MQTT command contract.
 */
#include "protocol_types.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static protocol_command_type_t command_type_from_string(const char *value)
{
    if (!value) return PROTOCOL_CMD_INVALID;
    if (strcmp(value, "accepted") == 0) return PROTOCOL_CMD_ACCEPTED;
    if (strcmp(value, "assigned") == 0) return PROTOCOL_CMD_ASSIGNED;
    if (strcmp(value, "locked") == 0) return PROTOCOL_CMD_LOCKED;
    if (strcmp(value, "completed") == 0) return PROTOCOL_CMD_COMPLETED;
    if (strcmp(value, "cancel_ack") == 0) return PROTOCOL_CMD_CANCEL_ACK;
    if (strcmp(value, "rejected") == 0) return PROTOCOL_CMD_REJECTED;
    if (strcmp(value, "overdue") == 0) return PROTOCOL_CMD_OVERDUE;
    if (strcmp(value, "sync") == 0) return PROTOCOL_CMD_SYNC;
    if (strcmp(value, "config") == 0) return PROTOCOL_CMD_CONFIG;
    return PROTOCOL_CMD_INVALID;
}

static reject_reason_t reject_reason_from_string(const char *value)
{
    if (!value) return REJECT_REASON_NONE;
    if (strcmp(value, "locked") == 0) return REJECT_REASON_LOCKED;
    if (strcmp(value, "duplicate") == 0) return REJECT_REASON_DUPLICATE;
    if (strcmp(value, "no_task") == 0) return REJECT_REASON_NO_TASK;
    if (strcmp(value, "wcs_busy") == 0) return REJECT_REASON_WCS_BUSY;
    return REJECT_REASON_NONE;
}

const char *protocol_command_name(protocol_command_type_t type)
{
    static const char *const names[] = {
        "invalid", "accepted", "assigned", "locked", "completed",
        "cancel_ack", "rejected", "overdue", "sync", "config"
    };
    return type < (sizeof(names) / sizeof(names[0])) ? names[type] : names[0];
}

const char *protocol_reject_reason_name(reject_reason_t reason)
{
    static const char *const names[] = {
        "none", "locked", "duplicate", "no_task", "wcs_busy"
    };
    return reason < (sizeof(names) / sizeof(names[0])) ? names[reason] : names[0];
}

static const char *json_value_start(const char *payload, const char *key)
{
    char needle[32];
    if (snprintf(needle, sizeof(needle), "\"%s\"", key) <= 0) return NULL;
    const char *cursor = strstr(payload, needle);
    if (!cursor) return NULL;
    cursor = strchr(cursor + strlen(needle), ':');
    if (!cursor) return NULL;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static bool json_read_u32(const char *payload, const char *key, uint32_t *value)
{
    const char *start = json_value_start(payload, key);
    if (!start || !isdigit((unsigned char)*start)) return false;
    char *end = NULL;
    const unsigned long parsed = strtoul(start, &end, 10);
    if (!end || end == start || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool json_read_string(const char *payload, const char *key, char *value, size_t value_size)
{
    const char *start = json_value_start(payload, key);
    if (!start || *start != '"' || value_size == 0) return false;
    start++;
    const char *end = strchr(start, '"');
    if (!end || (size_t)(end - start) >= value_size) return false;
    memcpy(value, start, (size_t)(end - start));
    value[end - start] = '\0';
    return true;
}

static bool protocol_parse_task_state(const char *value, TaskState_t *state)
{
    if (!value || !state) return false;
    if (strcmp(value, "idle") == 0) *state = TASK_IDLE;
    else if (strcmp(value, "queued") == 0) *state = TASK_QUEUED;
    else if (strcmp(value, "assigned") == 0) *state = TASK_ASSIGNED;
    else if (strcmp(value, "locked") == 0) *state = TASK_LOCKED;
    else if (strcmp(value, "completed") == 0) *state = TASK_COMPLETED;
    else return false;
    return true;
}

bool protocol_parse_command_json(const char *payload, protocol_command_t *command)
{
    if (!payload || !command) return false;
    memset(command, 0, sizeof(*command));
    char type[20] = {0};
    uint32_t task = 0;

    if (!json_read_string(payload, "type", type, sizeof(type))) return false;
    command->type = command_type_from_string(type);
    if (command->type == PROTOCOL_CMD_INVALID) return false;
    if (command->type != PROTOCOL_CMD_SYNC &&
        (!json_read_u32(payload, "task", &task) || task < 1 || task > 2)) {
        return false;
    }
    command->task = (int)task;
    (void)json_read_u32(payload, "ref_seq", &command->ref_seq);
    (void)json_read_u32(payload, "ts", &command->timestamp);
    (void)json_read_string(payload, "agv_id", command->agv_id, sizeof(command->agv_id));
    char reason[16] = {0};
    if (json_read_string(payload, "reason", reason, sizeof(reason))) {
        command->reason = reject_reason_from_string(reason);
    }

    if (command->type == PROTOCOL_CMD_SYNC) {
        char state[16] = {0};
        for (int i = 0; i < 2; ++i) {
            char key[20];
            snprintf(key, sizeof(key), "task%d_state", i + 1);
            if (!json_read_string(payload, key, state, sizeof(state)) ||
                !protocol_parse_task_state(state, &command->sync_state[i])) {
                return false;
            }
            snprintf(key, sizeof(key), "task%d_seq", i + 1);
            (void)json_read_u32(payload, key, &command->sync_call_seq[i]);
            snprintf(key, sizeof(key), "task%d_agv_id", i + 1);
            (void)json_read_string(payload, key, command->sync_agv_id[i],
                                   sizeof(command->sync_agv_id[i]));
        }
    }

    return true;
}
