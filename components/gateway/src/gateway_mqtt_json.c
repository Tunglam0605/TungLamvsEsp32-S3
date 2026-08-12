#include "gateway_mqtt_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FNV1A64_OFFSET UINT64_C(14695981039346656037)
#define FNV1A64_PRIME UINT64_C(1099511628211)
#define LAYOUT_HASH_MASK UINT64_C(0x0000FFFFFFFFFFFF)

typedef enum {
    WIRE_STATE_EMPTY = 0,
    WIRE_STATE_OCCUPIED,
    WIRE_STATE_UNKNOWN,
    WIRE_STATE_FAULT,
    WIRE_STATE_COUNT,
} wire_state_t;

typedef struct {
    wire_state_t states[WAREHOUSE_POSITION_MAX];
    uint8_t empty;
    uint8_t occupied;
    uint8_t unknown;
    uint8_t fault;
} wire_snapshot_t;

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
            if ((unsigned char)*text < 0x20U) {
                error = append(writer, "\\u%04x", (unsigned int)(unsigned char)*text);
            }
            else error = append(writer, "%c", *text);
            break;
        }
    }
    return error == ESP_OK ? append(writer, "\"") : error;
}

static bool snapshot_valid(const warehouse_snapshot_t *snapshot)
{
    return snapshot != NULL && laser_profile_valid(snapshot->profile) &&
           snapshot->group_count == laser_profile_group_count(snapshot->profile) &&
           snapshot->group_count <= WAREHOUSE_POSITION_MAX;
}

static wire_state_t derive_wire_state(const warehouse_position_t *position)
{
    if (!position->config.enabled || position->config.group_id == 0U ||
        position->config.laser_id == 0U) {
        return WIRE_STATE_UNKNOWN;
    }
    if (!position->sensor_online) return WIRE_STATE_FAULT;
    if (!position->status_valid) return WIRE_STATE_UNKNOWN;
    if (position->state == WAREHOUSE_STATE_EMPTY) return WIRE_STATE_EMPTY;
    if (position->state == WAREHOUSE_STATE_OCCUPIED) return WIRE_STATE_OCCUPIED;
    return WIRE_STATE_UNKNOWN;
}

static esp_err_t derive_wire_snapshot(const warehouse_snapshot_t *snapshot,
                                      wire_snapshot_t *wire)
{
    if (!snapshot_valid(snapshot) || wire == NULL) return ESP_ERR_INVALID_ARG;
    memset(wire, 0, sizeof(*wire));
    for (uint8_t i = 0; i < snapshot->group_count; ++i) {
        wire->states[i] = derive_wire_state(&snapshot->positions[i]);
        switch (wire->states[i]) {
        case WIRE_STATE_EMPTY: ++wire->empty; break;
        case WIRE_STATE_OCCUPIED: ++wire->occupied; break;
        case WIRE_STATE_FAULT: ++wire->fault; break;
        case WIRE_STATE_UNKNOWN: ++wire->unknown; break;
        default: return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

static void fnv1a64_bytes(uint64_t *hash, const void *bytes, size_t length)
{
    const uint8_t *cursor = (const uint8_t *)bytes;
    for (size_t i = 0; i < length; ++i) {
        *hash ^= cursor[i];
        *hash *= FNV1A64_PRIME;
    }
}

static void fnv1a64_u8(uint64_t *hash, uint8_t value)
{
    fnv1a64_bytes(hash, &value, sizeof(value));
}

static esp_err_t layout_version(const warehouse_snapshot_t *snapshot,
                                char output[GATEWAY_MQTT_LAYOUT_VERSION_HEX_LEN + 1U])
{
    if (!snapshot_valid(snapshot) || output == NULL) return ESP_ERR_INVALID_ARG;

    /* Stable canonical WAREHOUSE_LAYOUT_V1 input. Namespace identity,
     * warehouse_name and all live sensor fields are deliberately excluded.
     * Each slot contributes its ordinal, configured position ID, enabled flag,
     * physical group, Laser ID and length-prefixed warehouse code. */
    uint64_t hash = FNV1A64_OFFSET;
    static const char domain[] = "WAREHOUSE_LAYOUT_V1";
    fnv1a64_bytes(&hash, domain, sizeof(domain) - 1U);
    fnv1a64_u8(&hash, (uint8_t)snapshot->profile);
    fnv1a64_u8(&hash, snapshot->group_count);
    for (uint8_t i = 0; i < snapshot->group_count; ++i) {
        const warehouse_position_config_t *config = &snapshot->positions[i].config;
        const size_t code_length = strnlen(config->warehouse_code,
                                           sizeof(config->warehouse_code));
        if (code_length >= sizeof(config->warehouse_code)) return ESP_ERR_INVALID_ARG;
        fnv1a64_u8(&hash, (uint8_t)(i + 1U));
        fnv1a64_u8(&hash, config->position_id);
        fnv1a64_u8(&hash, config->enabled ? 1U : 0U);
        fnv1a64_u8(&hash, config->group_id);
        fnv1a64_u8(&hash, config->laser_id);
        fnv1a64_u8(&hash, (uint8_t)code_length);
        fnv1a64_bytes(&hash, config->warehouse_code, code_length);
    }
    const int written = snprintf(output, GATEWAY_MQTT_LAYOUT_VERSION_HEX_LEN + 1U,
                                 "%012llx",
                                 (unsigned long long)(hash & LAYOUT_HASH_MASK));
    return written == (int)GATEWAY_MQTT_LAYOUT_VERSION_HEX_LEN
        ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static const char *wire_state_name(wire_state_t state)
{
    static const char *const names[WIRE_STATE_COUNT] = {
        "EMPTY", "OCCUPIED", "UNKNOWN", "FAULT",
    };
    return state < WIRE_STATE_COUNT ? names[state] : "UNKNOWN";
}

static esp_err_t encode_status_bits(char *buffer, size_t capacity,
                                    const warehouse_snapshot_t *snapshot,
                                    const wire_snapshot_t *wire,
                                    size_t *length)
{
    static const char pairs[WIRE_STATE_COUNT][2] = {
        {'0', '0'}, {'0', '1'}, {'1', '0'}, {'1', '1'},
    };
    if (buffer == NULL || length == NULL || !snapshot_valid(snapshot) || wire == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *length = 0U;
    const size_t required = (size_t)snapshot->group_count * 2U;
    if (capacity < required) {
        if (capacity > 0U) buffer[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    for (uint8_t i = 0; i < snapshot->group_count; ++i) {
        const wire_state_t state = wire->states[i];
        if (state >= WIRE_STATE_COUNT) return ESP_ERR_INVALID_STATE;
        buffer[(size_t)i * 2U] = pairs[state][0];
        buffer[(size_t)i * 2U + 1U] = pairs[state][1];
    }
    if (capacity > required) buffer[required] = '\0';
    *length = required;
    return ESP_OK;
}

static bool utc_timestamp(int64_t timestamp, char output[28])
{
    if (timestamp <= 0) return false;
    const time_t value = (time_t)timestamp;
    if ((int64_t)value != timestamp) return false;
    struct tm utc = {0};
    if (gmtime_r(&value, &utc) == NULL) return false;
    /* The runtime context currently carries epoch seconds. Keep the common
     * Vision/Sensor RFC3339 shape by expressing the unavailable sub-second
     * component explicitly as six zero digits. */
    return strftime(output, 28U, "%Y-%m-%dT%H:%M:%S.000000Z", &utc) == 27U;
}

esp_err_t gateway_mqtt_json_snapshot(char *buffer, size_t capacity,
                                     const gateway_mqtt_json_context_t *context,
                                     const warehouse_snapshot_t *snapshot,
                                     size_t *length)
{
    if (buffer == NULL || capacity == 0U || context == NULL ||
        context->company_id == NULL || context->site_id == NULL ||
        context->warehouse_id == NULL || context->warehouse_name == NULL ||
        !snapshot_valid(snapshot) || length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *length = 0U;
    buffer[0] = '\0';

    wire_snapshot_t wire = {0};
    esp_err_t error = derive_wire_snapshot(snapshot, &wire);
    char state_bits[GATEWAY_MQTT_STATUS_BITS_MAX + 1U];
    size_t state_bits_length = 0U;
    if (error == ESP_OK) {
        error = encode_status_bits(state_bits, sizeof(state_bits), snapshot,
                                   &wire, &state_bits_length);
    }
    char version[GATEWAY_MQTT_LAYOUT_VERSION_HEX_LEN + 1U];
    if (error == ESP_OK) error = layout_version(snapshot, version);

    json_writer_t writer = {.buffer = buffer, .capacity = capacity};
    if (error == ESP_OK) error = append(&writer,
        "{\"schema\":\"WAREHOUSE_STATUS_V1\",\"source_type\":\"sensor\","
        "\"company_id\":");
    if (error == ESP_OK) error = append_escaped(&writer, context->company_id);
    if (error == ESP_OK) error = append(&writer, ",\"site_id\":");
    if (error == ESP_OK) error = append_escaped(&writer, context->site_id);
    if (error == ESP_OK) error = append(&writer, ",\"warehouse_id\":");
    if (error == ESP_OK) error = append_escaped(&writer, context->warehouse_id);
    if (error == ESP_OK) error = append(&writer, ",\"warehouse_name\":");
    if (error == ESP_OK) error = append_escaped(&writer, context->warehouse_name);
    if (error == ESP_OK) error = append(&writer,
        ",\"slot_count\":%u,"
        "\"order\":\"left_to_right_top_to_bottom\",\"state_bits\":\"%.*s\","
        "\"states\":[",
        snapshot->group_count, (int)state_bits_length, state_bits);
    for (uint8_t i = 0; error == ESP_OK && i < snapshot->group_count; ++i) {
        error = append(&writer, "%s\"%s\"", i == 0U ? "" : ",",
                       wire_state_name(wire.states[i]));
    }
    if (error == ESP_OK) {
        error = append(&writer,
            "],\"occupied_count\":%u,\"empty_count\":%u,\"unknown_count\":%u,"
            "\"fault_count\":%u,\"sequence\":%lu,\"layout_version\":\"%s\","
            "\"generated_at\":",
            wire.occupied, wire.empty, wire.unknown, wire.fault,
            (unsigned long)context->sequence, version);
    }
    if (error == ESP_OK) {
        char generated_at[28];
        if (utc_timestamp(context->timestamp, generated_at)) {
            error = append_escaped(&writer, generated_at);
        } else {
            error = append(&writer, "null");
        }
    }
    if (error == ESP_OK) error = append(&writer, "}");
    if (error == ESP_OK) *length = writer.used;
    return error;
}

esp_err_t gateway_mqtt_status_bits(char *buffer, size_t capacity,
                                   const warehouse_snapshot_t *snapshot,
                                   size_t *length)
{
    if (length != NULL) *length = 0U;
    wire_snapshot_t wire = {0};
    const esp_err_t error = derive_wire_snapshot(snapshot, &wire);
    return error == ESP_OK
        ? encode_status_bits(buffer, capacity, snapshot, &wire, length) : error;
}
