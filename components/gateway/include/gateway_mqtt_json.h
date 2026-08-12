#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "warehouse_manager.h"

#define GATEWAY_MQTT_JSON_MAX 4096U

typedef enum {
    GATEWAY_MQTT_COMMAND_NONE = 0,
    GATEWAY_MQTT_COMMAND_REQUEST_SNAPSHOT,
    GATEWAY_MQTT_COMMAND_PING,
} gateway_mqtt_command_t;

typedef struct {
    const char *gateway_id;
    const char *boot_id;
    uint32_t sequence;
    int64_t timestamp;
} gateway_mqtt_json_context_t;

esp_err_t gateway_mqtt_json_snapshot(char *buffer, size_t capacity,
                                     const gateway_mqtt_json_context_t *context,
                                     const warehouse_snapshot_t *snapshot,
                                     size_t *length);
esp_err_t gateway_mqtt_json_availability(char *buffer, size_t capacity,
                                         const char *gateway_id, bool online,
                                         int64_t timestamp, size_t *length);
esp_err_t gateway_mqtt_json_warehouse_event(char *buffer, size_t capacity,
                                            const gateway_mqtt_json_context_t *context,
                                            const warehouse_position_t *position,
                                            warehouse_state_t previous,
                                            size_t *length);
esp_err_t gateway_mqtt_json_ping(char *buffer, size_t capacity,
                                 const gateway_mqtt_json_context_t *context,
                                 size_t *length);
gateway_mqtt_command_t gateway_mqtt_json_parse_command(const char *json,
                                                       size_t length);
