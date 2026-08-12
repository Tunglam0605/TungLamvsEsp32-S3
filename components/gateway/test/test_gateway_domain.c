#include "unity.h"
#include "laser_profile.h"
#include "warehouse_manager.h"
#include "gateway_network.h"

TEST_CASE("laser profile exact boundaries", "[gateway]")
{
    uint8_t group = 0;
    TEST_ASSERT_TRUE(laser_profile_group_for_id(LASER_PROFILE_GROUP_8, 46, &group));
    TEST_ASSERT_EQUAL_UINT8(5, group);
    TEST_ASSERT_TRUE(laser_profile_group_for_id(LASER_PROFILE_GROUP_8, 47, &group));
    TEST_ASSERT_EQUAL_UINT8(6, group);
    TEST_ASSERT_TRUE(laser_profile_group_for_id(LASER_PROFILE_GROUP_12, 43, &group));
    TEST_ASSERT_EQUAL_UINT8(5, group);
    TEST_ASSERT_TRUE(laser_profile_group_for_id(LASER_PROFILE_GROUP_12, 44, &group));
    TEST_ASSERT_EQUAL_UINT8(6, group);
    TEST_ASSERT_TRUE(laser_profile_id_allowed(LASER_PROFILE_GROUP_12, 12, 64));
    TEST_ASSERT_FALSE(laser_profile_id_allowed(LASER_PROFILE_GROUP_12, 12, 61));
}

TEST_CASE("only Ethernet uplink counts as production network", "[gateway]")
{
    TEST_ASSERT_FALSE(gateway_network_production_state(false, true, false));
    TEST_ASSERT_TRUE(gateway_network_production_state(false, true, true));
    TEST_ASSERT_TRUE(gateway_network_production_state(true, false, false));
    TEST_ASSERT_FALSE(gateway_network_production_state(false, false, true));
}

TEST_CASE("warehouse offline is unknown", "[gateway]")
{
    TEST_ASSERT_EQUAL(WAREHOUSE_STATE_UNKNOWN, warehouse_state_from_sensor(false, true, LASER_OBSTACLE_CLEAR));
    TEST_ASSERT_EQUAL(WAREHOUSE_STATE_UNKNOWN, warehouse_state_from_sensor(true, false, LASER_OBSTACLE_CLEAR));
    TEST_ASSERT_EQUAL(WAREHOUSE_STATE_EMPTY, warehouse_state_from_sensor(true, true, LASER_OBSTACLE_CLEAR));
    TEST_ASSERT_EQUAL(WAREHOUSE_STATE_OCCUPIED, warehouse_state_from_sensor(true, true, LASER_OBSTACLE_EMERGENCY));
    TEST_ASSERT_EQUAL(WAREHOUSE_STATE_OCCUPIED, warehouse_state_from_sensor(true, true, LASER_OBSTACLE_NORMAL));
}
