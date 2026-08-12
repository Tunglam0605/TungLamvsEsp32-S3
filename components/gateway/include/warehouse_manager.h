#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "laser_can_bringup.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAREHOUSE_MAX_CLUSTERS 16U
#define WAREHOUSE_SLOT_CODE_LEN 16U
#define WAREHOUSE_SLOT_NAME_LEN 32U

typedef enum {
    WAREHOUSE_SLOT_OFFLINE = 0,
    WAREHOUSE_SLOT_WAITING,
    WAREHOUSE_SLOT_EMPTY,
    WAREHOUSE_SLOT_OCCUPIED,
    WAREHOUSE_SLOT_CRITICAL,
} warehouse_slot_state_t;

typedef struct {
    bool assigned;
    uint8_t laser_id;
    uint8_t slot_index; /* 1..64: thu tu bit logic tren MQTT, doc lap LaserID */
    uint8_t cluster_id;
    char slot_code[WAREHOUSE_SLOT_CODE_LEN];
    char slot_name[WAREHOUSE_SLOT_NAME_LEN];
} warehouse_mapping_t;

typedef struct {
    warehouse_mapping_t mapping;
    bool sensor_detected;
    bool sensor_online;
    uint8_t b300_group;
    warehouse_slot_state_t state;
    int64_t last_seen_ms;
    bool proximity_enabled;
    uint16_t distance_mm;
    uint16_t distance_emergency_mm;
} warehouse_slot_status_t;

typedef struct {
    uint16_t assigned;
    uint16_t online;
    uint16_t offline;
    uint16_t empty;
    uint16_t occupied;
    uint16_t critical;
} warehouse_summary_t;

esp_err_t warehouse_manager_init(void);
esp_err_t warehouse_manager_set_mapping(const warehouse_mapping_t *mapping);
esp_err_t warehouse_manager_clear_mapping(uint8_t laser_id);
bool warehouse_manager_get_mapping(uint8_t laser_id, warehouse_mapping_t *mapping);
size_t warehouse_manager_get_slots(warehouse_slot_status_t *slots, size_t capacity,
                                   warehouse_summary_t *summary);
const char *warehouse_slot_state_name(warehouse_slot_state_t state);

#ifdef __cplusplus
}
#endif
