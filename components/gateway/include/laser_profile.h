#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LASER_PROFILE_MAX_GROUPS 12U
#define LASER_ID_MIN 1U
#define LASER_ID_MAX 64U

typedef enum {
    LASER_PROFILE_GROUP_8 = 0,
    LASER_PROFILE_GROUP_12 = 1,
} laser_profile_t;

typedef struct {
    uint8_t group_id;
    uint8_t laser_id_first;
    uint8_t laser_id_last;
    uint16_t emergency_can_id;
    uint16_t normal_can_id;
} laser_group_definition_t;

typedef struct {
    laser_profile_t profile;
    uint8_t group_count;
    uint16_t emergency_event_base;
    uint16_t normal_event_base;
    const laser_group_definition_t *groups;
} laser_profile_definition_t;

const laser_profile_definition_t *laser_profile_definition(laser_profile_t profile);
bool laser_profile_valid(laser_profile_t profile);
uint8_t laser_profile_group_count(laser_profile_t profile);
bool laser_profile_group_definition(laser_profile_t profile, uint8_t group_id,
                                    laser_group_definition_t *definition);
bool laser_profile_group_for_id(laser_profile_t profile, uint8_t laser_id,
                                uint8_t *group_id);
bool laser_profile_id_allowed(laser_profile_t profile, uint8_t group_id,
                              uint8_t laser_id);
bool laser_profile_group_for_obstacle_can_id(laser_profile_t profile,
                                              uint16_t can_id,
                                              uint8_t *group_id,
                                              bool *emergency);
const char *laser_profile_name(laser_profile_t profile);
