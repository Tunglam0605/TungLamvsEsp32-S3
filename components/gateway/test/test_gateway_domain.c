#include "unity.h"
#include <stdio.h>
#include <string.h>
#include "laser_profile.h"
#include "warehouse_manager.h"
#include "gateway_network.h"
#include "gateway_mqtt_json.h"

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

TEST_CASE("laser obstacle event IDs follow each profile", "[gateway]")
{
    laser_group_definition_t group = {0};
    TEST_ASSERT_TRUE(laser_profile_group_definition(LASER_PROFILE_GROUP_8, 1, &group));
    TEST_ASSERT_EQUAL_UINT16(100, group.emergency_can_id);
    TEST_ASSERT_EQUAL_UINT16(110, group.normal_can_id);
    TEST_ASSERT_TRUE(laser_profile_group_definition(LASER_PROFILE_GROUP_8, 8, &group));
    TEST_ASSERT_EQUAL_UINT16(107, group.emergency_can_id);
    TEST_ASSERT_EQUAL_UINT16(117, group.normal_can_id);
    TEST_ASSERT_TRUE(laser_profile_group_definition(LASER_PROFILE_GROUP_12, 1, &group));
    TEST_ASSERT_EQUAL_UINT16(90, group.emergency_can_id);
    TEST_ASSERT_EQUAL_UINT16(105, group.normal_can_id);
    TEST_ASSERT_TRUE(laser_profile_group_definition(LASER_PROFILE_GROUP_12, 12, &group));
    TEST_ASSERT_EQUAL_UINT16(101, group.emergency_can_id);
    TEST_ASSERT_EQUAL_UINT16(116, group.normal_can_id);
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

TEST_CASE("warehouse candidate rejects duplicates and invalid distance", "[gateway]")
{
    warehouse_position_config_t positions[2] = {
        {.enabled=true,.group_id=2,.laser_id=1,.warehouse_code="KHO-A01",
         .distance_mm=600,.distance_emergency_mm=300},
        {.enabled=true,.group_id=2,.laser_id=11,.warehouse_code="KHO-A02",
         .distance_mm=600,.distance_emergency_mm=300},
    };
    warehouse_position_config_t candidate = {
        .enabled=true,.group_id=1,.laser_id=1,.warehouse_code="KHO-B01",
        .distance_mm=600,.distance_emergency_mm=300,
    };
    TEST_ASSERT_EQUAL(WAREHOUSE_DUPLICATE_LASER_ID,
        warehouse_manager_validate_candidate(LASER_PROFILE_GROUP_8, positions, 2, &candidate));
    candidate.group_id = 2;
    candidate.laser_id = 11;
    strlcpy(candidate.warehouse_code, "KHO-A01", sizeof(candidate.warehouse_code));
    positions[0].group_id = 1;
    positions[0].laser_id = 1;
    TEST_ASSERT_EQUAL(WAREHOUSE_DUPLICATE_CODE,
        warehouse_manager_validate_candidate(LASER_PROFILE_GROUP_8, positions, 2, &candidate));
    strlcpy(candidate.warehouse_code, "KHO-B02", sizeof(candidate.warehouse_code));
    candidate.distance_emergency_mm = 700;
    TEST_ASSERT_EQUAL(WAREHOUSE_INVALID_DISTANCE,
        warehouse_manager_validate_candidate(LASER_PROFILE_GROUP_8, positions, 2, &candidate));
}

TEST_CASE("production MQTT supports twelve enabled positions", "[gateway]")
{
    warehouse_snapshot_t snapshot = {.profile=LASER_PROFILE_GROUP_12,.group_count=12};
    for (uint8_t i = 0; i < 12; ++i) {
        snapshot.positions[i].config.enabled = true;
        snapshot.positions[i].config.group_id = i + 1U;
        snapshot.positions[i].config.laser_id = i + 1U;
        snprintf(snapshot.positions[i].config.warehouse_code,
                 sizeof(snapshot.positions[i].config.warehouse_code), "KHO-%02u", i + 1U);
        snapshot.positions[i].state = i < 4U ? WAREHOUSE_STATE_EMPTY :
            i < 9U ? WAREHOUSE_STATE_OCCUPIED : WAREHOUSE_STATE_UNKNOWN;
    }
    char json[GATEWAY_MQTT_JSON_MAX];
    size_t length = 0;
    const gateway_mqtt_json_context_t context = {
        .gateway_id="GW-12",.boot_id="12345678",.sequence=1};
    TEST_ASSERT_EQUAL(ESP_OK, gateway_mqtt_json_snapshot(
        json, sizeof(json), &context, &snapshot, &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"profile\":\"12_group\""));
    TEST_ASSERT_NOT_NULL(strstr(json,
        "\"summary\":{\"total\":12,\"empty\":4,\"occupied\":5,\"unknown\":3}"));
}

TEST_CASE("production MQTT snapshot serializes only enabled positions", "[gateway]")
{
    warehouse_snapshot_t snapshot = {.profile=LASER_PROFILE_GROUP_8,.group_count=8};
    snapshot.positions[0].config = (warehouse_position_config_t) {
        .enabled=true,.group_id=1,.laser_id=1,.warehouse_code="KHO-A01"};
    snapshot.positions[0].state = WAREHOUSE_STATE_EMPTY;
    snapshot.positions[0].sensor_online = true;
    snapshot.positions[1].config = (warehouse_position_config_t) {
        .enabled=true,.group_id=2,.laser_id=11,.warehouse_code="KHO-A02"};
    snapshot.positions[1].state = WAREHOUSE_STATE_OCCUPIED;
    snapshot.positions[1].sensor_online = true;
    snapshot.positions[2].config = (warehouse_position_config_t) {
        .enabled=true,.group_id=3,.laser_id=21,.warehouse_code="KHO-A03"};
    snapshot.positions[2].state = WAREHOUSE_STATE_UNKNOWN;
    snapshot.positions[3].config = (warehouse_position_config_t) {
        .enabled=false,.group_id=4,.laser_id=31,.warehouse_code="KHONG-GUI"};
    snapshot.positions[8].config = (warehouse_position_config_t) {
        .enabled=true,.group_id=9,.laser_id=53,.warehouse_code="NGOAI-PROFILE"};
    char json[GATEWAY_MQTT_JSON_MAX];
    size_t length = 0;
    const gateway_mqtt_json_context_t context = {
        .gateway_id="GW-A01",.boot_id="8F31A2C4",.sequence=2001,.timestamp=1786501200};
    TEST_ASSERT_EQUAL(ESP_OK, gateway_mqtt_json_snapshot(
        json, sizeof(json), &context, &snapshot, &length));
    TEST_ASSERT_EQUAL_size_t(strlen(json), length);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"gateway_id\":\"GW-A01\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"profile\":\"8_group\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"seq\":2001"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"boot_id\":\"8F31A2C4\""));
    TEST_ASSERT_NOT_NULL(strstr(json,
        "\"summary\":{\"total\":3,\"empty\":1,\"occupied\":1,\"unknown\":1}"));
    TEST_ASSERT_NULL(strstr(json, "KHONG-GUI"));
    TEST_ASSERT_NULL(strstr(json, "NGOAI-PROFILE"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"state\":\"empty\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"state\":\"occupied\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"state\":\"unknown\""));
}

TEST_CASE("production MQTT availability includes logical gateway identity", "[gateway]")
{
    char json[160];
    size_t length = 0;
    TEST_ASSERT_EQUAL(ESP_OK, gateway_mqtt_json_availability(
        json, sizeof(json), "GW-A01", false, 0, &length));
    TEST_ASSERT_EQUAL_STRING("{\"online\":false,\"gateway_id\":\"GW-A01\"}", json);
    TEST_ASSERT_EQUAL_size_t(strlen(json), length);
}

TEST_CASE("production MQTT command parser is bounded and exact", "[gateway]")
{
    const char snapshot[] = "{\"cmd\":\"request_snapshot\"}";
    const char ping[] = "{ \"cmd\" : \"ping\" }";
    const char invalid[] = "{\"other\":\"request_snapshot\"}";
    TEST_ASSERT_EQUAL(GATEWAY_MQTT_COMMAND_REQUEST_SNAPSHOT,
        gateway_mqtt_json_parse_command(snapshot, strlen(snapshot)));
    TEST_ASSERT_EQUAL(GATEWAY_MQTT_COMMAND_PING,
        gateway_mqtt_json_parse_command(ping, strlen(ping)));
    TEST_ASSERT_EQUAL(GATEWAY_MQTT_COMMAND_NONE,
        gateway_mqtt_json_parse_command(invalid, strlen(invalid)));
}
