#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "laser_profile.h"
#include "laser_can_bringup.h"

#define WAREHOUSE_POSITION_MAX LASER_PROFILE_MAX_GROUPS
#define WAREHOUSE_CODE_LEN 16U
#define WAREHOUSE_NAME_LEN 32U

typedef enum {
    WAREHOUSE_STATE_UNKNOWN = 0,
    WAREHOUSE_STATE_EMPTY,
    WAREHOUSE_STATE_OCCUPIED,
} warehouse_state_t;

typedef enum {
    WAREHOUSE_VALID = 0,
    WAREHOUSE_INVALID_GROUP,
    WAREHOUSE_INVALID_LASER_ID,
    WAREHOUSE_DUPLICATE_LASER_ID,
    WAREHOUSE_DUPLICATE_CODE,
    WAREHOUSE_INVALID_DISTANCE,
    WAREHOUSE_INVALID_TEXT,
    WAREHOUSE_PROFILE_CONFLICT,
} warehouse_validation_t;

typedef struct {
    bool enabled;
    uint8_t group_id;
    uint8_t laser_id;
    char warehouse_code[WAREHOUSE_CODE_LEN];
    char warehouse_name[WAREHOUSE_NAME_LEN];
    uint16_t distance_mm;
    uint16_t distance_emergency_mm;
    uint8_t low_col;
    uint8_t high_row;
    bool proximity_enabled;
} warehouse_position_config_t;

typedef struct {
    warehouse_position_config_t config;
    warehouse_state_t state;
    bool sensor_detected;
    bool sensor_online;
    bool status_valid;
    int64_t last_seen_ago_ms;
    uint16_t distance_mm;
    uint16_t distance_emergency_mm;
    laser_obstacle_state_t warn;
    laser_config_state_t config_state;
} warehouse_position_t;

typedef struct {
    laser_profile_t profile;
    uint8_t group_count;
    uint8_t configured;
    uint8_t online;
    uint8_t unknown;
    uint8_t empty;
    uint8_t occupied;
    warehouse_position_t positions[WAREHOUSE_POSITION_MAX];
} warehouse_snapshot_t;

esp_err_t warehouse_manager_init(void);
laser_profile_t warehouse_manager_profile(void);
warehouse_validation_t warehouse_manager_validate_profile(laser_profile_t profile);
esp_err_t warehouse_manager_set_profile(laser_profile_t profile, bool clear_conflicts);
warehouse_validation_t warehouse_manager_validate_position(
    const warehouse_position_config_t *position);
warehouse_validation_t warehouse_manager_validate_candidate(
    laser_profile_t profile, const warehouse_position_config_t *positions,
    size_t position_count, const warehouse_position_config_t *candidate);
esp_err_t warehouse_manager_set_position(const warehouse_position_config_t *position);
bool warehouse_manager_get_position(uint8_t group_id, warehouse_position_t *position);
void warehouse_manager_snapshot(warehouse_snapshot_t *snapshot);
warehouse_state_t warehouse_state_from_sensor(bool online, bool status_valid,
                                               laser_obstacle_state_t warn);
const char *warehouse_state_name(warehouse_state_t state);
const char *warehouse_validation_name(warehouse_validation_t result);
