#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "warehouse_manager.h"

#define GATEWAY_MQTT_JSON_MAX 4096U
#define GATEWAY_MQTT_STATUS_BITS_MAX (WAREHOUSE_POSITION_MAX * 2U)
#define GATEWAY_MQTT_LAYOUT_VERSION_HEX_LEN 12U

typedef struct {
    const char *company_id;
    const char *site_id;
    const char *warehouse_id;
    const char *warehouse_name;
    uint32_t sequence;
    int64_t timestamp;
} gateway_mqtt_json_context_t;

esp_err_t gateway_mqtt_json_snapshot(char *buffer, size_t capacity,
                                     const gateway_mqtt_json_context_t *context,
                                     const warehouse_snapshot_t *snapshot,
                                     size_t *length);
/* Encode the same ordered slot state used by WAREHOUSE_STATUS_V1 as raw ASCII
 * bit pairs: EMPTY=00, OCCUPIED=01, UNKNOWN=10 and FAULT=11. The returned
 * length never includes a trailing NUL. A NUL is added only when capacity is
 * greater than the payload length. */
esp_err_t gateway_mqtt_status_bits(char *buffer, size_t capacity,
                                   const warehouse_snapshot_t *snapshot,
                                   size_t *length);
