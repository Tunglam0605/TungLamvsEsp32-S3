#include "gateway_mqtt_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
} json_writer_t;

static esp_err_t append(json_writer_t *writer, const char *format, ...)
{
    if (writer->used >= writer->capacity) return ESP_ERR_INVALID_SIZE;
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(writer->buffer + writer->used,
                                  writer->capacity - writer->used,
                                  format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= writer->capacity - writer->used) {
        return ESP_ERR_INVALID_SIZE;
    }
    writer->used += (size_t)written;
    return ESP_OK;
}

static esp_err_t append_escaped(json_writer_t *writer, const char *text)
{
    esp_err_t error = append(writer, "\"");
    for (; error == ESP_OK && text != NULL && *text != '\0'; ++text) {
        switch (*text) {
        case '"': error = append(writer, "\\\""); break;
        case '\\': error = append(writer, "\\\\"); break;
        case '\b': error = append(writer, "\\b"); break;
        case '\f': error = append(writer, "\\f"); break;
        case '\n': error = append(writer, "\\n"); break;
        case '\r': error = append(writer, "\\r"); break;
        case '\t': error = append(writer, "\\t"); break;
        default:
            if ((unsigned char)*text < 0x20U) error = append(writer, "\\u%04x", *text);
            else error = append(writer, "%c", *text);
            break;
        }
    }
    return error == ESP_OK ? append(writer, "\"") : error;
}

static const char *profile_json_name(laser_profile_t profile)
{
    return profile == LASER_PROFILE_GROUP_12 ? "12_group" : "8_group";
}

static const char *state_json_name(warehouse_state_t state)
{
    if (state == WAREHOUSE_STATE_EMPTY) return "empty";
    if (state == WAREHOUSE_STATE_OCCUPIED) return "occupied";
    return "unknown";
}

static esp_err_t append_context(json_writer_t *writer,
                                const gateway_mqtt_json_context_t *context)
{
    esp_err_t error = append(writer, "\"gateway_id\":");
    if (error == ESP_OK) error = append_escaped(writer, context->gateway_id);
    if (error == ESP_OK) error = append(writer, ",\"seq\":%lu,\"boot_id\":",
                                        (unsigned long)context->sequence);
    if (error == ESP_OK) error = append_escaped(writer, context->boot_id);
    if (error == ESP_OK && context->timestamp > 0) {
        error = append(writer, ",\"ts\":%lld", (long long)context->timestamp);
    }
    return error;
}

esp_err_t gateway_mqtt_json_snapshot(char *buffer, size_t capacity,
                                     const gateway_mqtt_json_context_t *context,
                                     const warehouse_snapshot_t *snapshot,
                                     size_t *length)
{
    if (buffer == NULL || context == NULL || context->gateway_id == NULL ||
        context->boot_id == NULL || snapshot == NULL || length == NULL ||
        snapshot->group_count > WAREHOUSE_POSITION_MAX ||
        !laser_profile_valid(snapshot->profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t total = 0, empty = 0, occupied = 0, unknown = 0;
    for (uint8_t i = 0; i < snapshot->group_count; ++i) {
        const warehouse_position_t *position = &snapshot->positions[i];
        if (!position->config.enabled) continue;
        ++total;
        if (position->state == WAREHOUSE_STATE_EMPTY) ++empty;
        else if (position->state == WAREHOUSE_STATE_OCCUPIED) ++occupied;
        else ++unknown;
    }

    json_writer_t writer = {.buffer = buffer, .capacity = capacity};
    esp_err_t error = append(&writer, "{\"schema\":\"JSON_WAREHOUSE_V1\",\"type\":\"snapshot\",");
    if (error == ESP_OK) error = append_context(&writer, context);
    if (error == ESP_OK) error = append(&writer,
        ",\"profile\":\"%s\",\"summary\":{\"total\":%u,\"empty\":%u,"
        "\"occupied\":%u,\"unknown\":%u},\"positions\":[",
        profile_json_name(snapshot->profile), total, empty, occupied, unknown);

    bool first = true;
    for (uint8_t i = 0; error == ESP_OK && i < snapshot->group_count; ++i) {
        const warehouse_position_t *position = &snapshot->positions[i];
        if (!position->config.enabled) continue;
        error = append(&writer, "%s{\"group\":%u,\"warehouse_id\":",
                       first ? "" : ",", position->config.group_id);
        if (error == ESP_OK) error = append_escaped(&writer, position->config.warehouse_code);
        if (error == ESP_OK) error = append(&writer,
            ",\"laser_id\":%u,\"state\":\"%s\",\"sensor_online\":%s}",
            position->config.laser_id, state_json_name(position->state),
            position->sensor_online ? "true" : "false");
        first = false;
    }
    if (error == ESP_OK) error = append(&writer, "]}");
    if (error == ESP_OK) *length = writer.used;
    return error;
}

esp_err_t gateway_mqtt_json_availability(char *buffer, size_t capacity,
                                         const char *gateway_id, bool online,
                                         int64_t timestamp, size_t *length)
{
    if (buffer == NULL || gateway_id == NULL || length == NULL) return ESP_ERR_INVALID_ARG;
    json_writer_t writer = {.buffer = buffer, .capacity = capacity};
    esp_err_t error = append(&writer, "{\"online\":%s,\"gateway_id\":", online ? "true" : "false");
    if (error == ESP_OK) error = append_escaped(&writer, gateway_id);
    if (error == ESP_OK && timestamp > 0) error = append(&writer, ",\"ts\":%lld", (long long)timestamp);
    if (error == ESP_OK) error = append(&writer, "}");
    if (error == ESP_OK) *length = writer.used;
    return error;
}

esp_err_t gateway_mqtt_json_warehouse_event(char *buffer, size_t capacity,
                                            const gateway_mqtt_json_context_t *context,
                                            const warehouse_position_t *position,
                                            warehouse_state_t previous,
                                            size_t *length)
{
    if (buffer == NULL || context == NULL || context->gateway_id == NULL ||
        context->boot_id == NULL || position == NULL || length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    json_writer_t writer = {.buffer = buffer, .capacity = capacity};
    esp_err_t error = append(&writer, "{\"schema\":\"JSON_WAREHOUSE_V1\",\"type\":\"warehouse_state\",");
    if (error == ESP_OK) error = append_context(&writer, context);
    if (error == ESP_OK) error = append(&writer, ",\"position\":{\"group\":%u,\"warehouse_id\":",
                                        position->config.group_id);
    if (error == ESP_OK) error = append_escaped(&writer, position->config.warehouse_code);
    if (error == ESP_OK) error = append(&writer,
        ",\"laser_id\":%u,\"previous\":\"%s\",\"state\":\"%s\"}}",
        position->config.laser_id, state_json_name(previous),
        state_json_name(position->state));
    if (error == ESP_OK) *length = writer.used;
    return error;
}

esp_err_t gateway_mqtt_json_ping(char *buffer, size_t capacity,
                                 const gateway_mqtt_json_context_t *context,
                                 size_t *length)
{
    if (buffer == NULL || context == NULL || context->gateway_id == NULL ||
        context->boot_id == NULL || length == NULL) return ESP_ERR_INVALID_ARG;
    json_writer_t writer = {.buffer = buffer, .capacity = capacity};
    esp_err_t error = append(&writer, "{\"schema\":\"JSON_WAREHOUSE_V1\",\"type\":\"pong\",");
    if (error == ESP_OK) error = append_context(&writer, context);
    if (error == ESP_OK) error = append(&writer, "}");
    if (error == ESP_OK) *length = writer.used;
    return error;
}

gateway_mqtt_command_t gateway_mqtt_json_parse_command(const char *json,
                                                       size_t length)
{
    if (json == NULL || length == 0U || length >= 128U) return GATEWAY_MQTT_COMMAND_NONE;
    char copy[128];
    memcpy(copy, json, length);
    copy[length] = '\0';
    char *cursor = strstr(copy, "\"cmd\"");
    if (cursor == NULL) return GATEWAY_MQTT_COMMAND_NONE;
    cursor += strlen("\"cmd\"");
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor++ != ':') return GATEWAY_MQTT_COMMAND_NONE;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (strncmp(cursor, "\"request_snapshot\"", strlen("\"request_snapshot\"")) == 0) {
        return GATEWAY_MQTT_COMMAND_REQUEST_SNAPSHOT;
    }
    if (strncmp(cursor, "\"ping\"", strlen("\"ping\"")) == 0) {
        return GATEWAY_MQTT_COMMAND_PING;
    }
    return GATEWAY_MQTT_COMMAND_NONE;
}
