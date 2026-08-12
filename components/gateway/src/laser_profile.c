#include "laser_profile.h"

#include <stddef.h>

#define GROUP_8(id, first, last) \
    { (id), (first), (last), (uint16_t)(99U + (id)), (uint16_t)(109U + (id)) }
#define GROUP_12(id, first, last) { (id), (first), (last), 0U, 0U }

static const laser_group_definition_t GROUPS_8[] = {
    GROUP_8(1, 1, 10), GROUP_8(2, 11, 20), GROUP_8(3, 21, 30), GROUP_8(4, 31, 40),
    GROUP_8(5, 41, 46), GROUP_8(6, 47, 52), GROUP_8(7, 53, 58), GROUP_8(8, 59, 64),
};

static const laser_group_definition_t GROUPS_12[] = {
    /* Obstacle CAN IDs for 12-group firmware are not frozen in this repository.
     * Do not extend the old 100/110 bases: IDs 110..111 would be ambiguous. */
    GROUP_12(1, 1, 10), GROUP_12(2, 11, 20), GROUP_12(3, 21, 30), GROUP_12(4, 31, 40),
    GROUP_12(5, 41, 43), GROUP_12(6, 44, 46), GROUP_12(7, 47, 49), GROUP_12(8, 50, 52),
    GROUP_12(9, 53, 55), GROUP_12(10, 56, 58), GROUP_12(11, 59, 61), GROUP_12(12, 62, 64),
};

static const laser_profile_definition_t PROFILES[] = {
    { LASER_PROFILE_GROUP_8, 8U, GROUPS_8 },
    { LASER_PROFILE_GROUP_12, 12U, GROUPS_12 },
};

const laser_profile_definition_t *laser_profile_definition(laser_profile_t profile)
{
    return laser_profile_valid(profile) ? &PROFILES[(unsigned)profile] : NULL;
}

bool laser_profile_valid(laser_profile_t profile)
{
    return profile == LASER_PROFILE_GROUP_8 || profile == LASER_PROFILE_GROUP_12;
}

uint8_t laser_profile_group_count(laser_profile_t profile)
{
    const laser_profile_definition_t *definition = laser_profile_definition(profile);
    return definition == NULL ? 0U : definition->group_count;
}

bool laser_profile_group_definition(laser_profile_t profile, uint8_t group_id,
                                    laser_group_definition_t *definition)
{
    const laser_profile_definition_t *p = laser_profile_definition(profile);
    if (p == NULL || definition == NULL || group_id == 0U || group_id > p->group_count) {
        return false;
    }
    *definition = p->groups[group_id - 1U];
    return true;
}

bool laser_profile_group_for_id(laser_profile_t profile, uint8_t laser_id,
                                uint8_t *group_id)
{
    const laser_profile_definition_t *p = laser_profile_definition(profile);
    if (p == NULL || group_id == NULL || laser_id < LASER_ID_MIN || laser_id > LASER_ID_MAX) {
        return false;
    }
    for (uint8_t i = 0; i < p->group_count; ++i) {
        if (laser_id >= p->groups[i].laser_id_first && laser_id <= p->groups[i].laser_id_last) {
            *group_id = p->groups[i].group_id;
            return true;
        }
    }
    return false;
}

bool laser_profile_id_allowed(laser_profile_t profile, uint8_t group_id,
                              uint8_t laser_id)
{
    laser_group_definition_t group = { 0 };
    return laser_profile_group_definition(profile, group_id, &group) &&
           laser_id >= group.laser_id_first && laser_id <= group.laser_id_last;
}

bool laser_profile_group_for_obstacle_can_id(laser_profile_t profile,
                                              uint16_t can_id,
                                              uint8_t *group_id,
                                              bool *emergency)
{
    const laser_profile_definition_t *p = laser_profile_definition(profile);
    if (p == NULL || group_id == NULL || emergency == NULL) return false;
    for (uint8_t i = 0; i < p->group_count; ++i) {
        if (p->groups[i].emergency_can_id != 0U &&
            (can_id == p->groups[i].emergency_can_id || can_id == p->groups[i].normal_can_id)) {
            *group_id = p->groups[i].group_id;
            *emergency = can_id == p->groups[i].emergency_can_id;
            return true;
        }
    }
    return false;
}

const char *laser_profile_name(laser_profile_t profile)
{
    return profile == LASER_PROFILE_GROUP_8 ? "GROUP_8" :
           profile == LASER_PROFILE_GROUP_12 ? "GROUP_12" : "INVALID";
}
