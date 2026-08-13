#include "unity.h"
#include <stdio.h>
#include <string.h>
#include "laser_profile.h"
#include "warehouse_manager.h"
#include "gateway_network.h"
#include "gateway_config.h"
#include "gateway_topic.h"
#include "gateway_mqtt_json.h"
#include "gateway_auth.h"

static gateway_config_t mqtt_identity(const char *warehouse_id,
                                      const char *warehouse_name)
{
    gateway_config_t config = {0};
    strlcpy(config.gateway_id, "GW-01", sizeof(config.gateway_id));
    strlcpy(config.company_id, "aubot", sizeof(config.company_id));
    strlcpy(config.site_id, "ha-noi", sizeof(config.site_id));
    strlcpy(config.warehouse_id, warehouse_id, sizeof(config.warehouse_id));
    strlcpy(config.warehouse_name, warehouse_name, sizeof(config.warehouse_name));
    return config;
}

static warehouse_snapshot_t status_snapshot(laser_profile_t profile)
{
    warehouse_snapshot_t snapshot = {
        .profile = profile,
        .group_count = laser_profile_group_count(profile),
    };
    for (uint8_t i = 0U; i < snapshot.group_count; ++i) {
        snapshot.positions[i].config.position_id = i + 1U;
    }
    return snapshot;
}

static void configure_status_position(warehouse_snapshot_t *snapshot,
                                      uint8_t index, warehouse_state_t state,
                                      bool sensor_online, bool status_valid)
{
    warehouse_position_t *position = &snapshot->positions[index];
    position->config.enabled = true;
    position->config.group_id = index + 1U;
    position->config.laser_id = index + 1U;
    snprintf(position->config.warehouse_code,
             sizeof(position->config.warehouse_code), "KHO-%02u", index + 1U);
    position->state = state;
    position->sensor_online = sensor_online;
    position->status_valid = status_valid;
}

static gateway_mqtt_json_context_t mqtt_context(uint32_t sequence)
{
    return (gateway_mqtt_json_context_t) {
        .company_id = "aubot",
        .site_id = "ha-noi",
        .warehouse_id = "kho-vp",
        .warehouse_name = "Kho Văn Phòng",
        .sequence = sequence,
        .timestamp = 1786501230,
    };
}

static void assert_json_state_bits(const char *json, const char *bits)
{
    char expected[64];
    snprintf(expected, sizeof(expected), "\"state_bits\":\"%s\"", bits);
    TEST_ASSERT_NOT_NULL(strstr(json, expected));
}

static void assert_status_json_field_order(const char *json)
{
    static const char *const fields[] = {
        "\"schema\":",
        "\"source_type\":",
        "\"company_id\":",
        "\"site_id\":",
        "\"warehouse_id\":",
        "\"warehouse_name\":",
        "\"slot_count\":",
        "\"order\":",
        "\"state_bits\":",
        "\"states\":",
        "\"occupied_count\":",
        "\"empty_count\":",
        "\"unknown_count\":",
        "\"fault_count\":",
        "\"sequence\":",
        "\"layout_version\":",
        "\"generated_at\":",
    };
    const char *cursor = json;
    for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        cursor = strstr(cursor, fields[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(cursor,
            "WAREHOUSE_STATUS_V1 field is missing or out of Vision-compatible order");
        cursor += strlen(fields[i]);
    }
}

TEST_CASE("gateway role permission matrix is exact", "[gateway][auth]")
{
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_NONE,
        GW_PERMISSION_WAREHOUSE_CONFIG));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_NONE,
        GW_PERMISSION_CAN_DEBUG));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_NONE,
        GW_PERMISSION_NETWORK_CONFIG));

    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_FACTORY,
        GW_PERMISSION_WAREHOUSE_CONFIG));
    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_FACTORY,
        GW_PERMISSION_LASER_CONFIG));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_FACTORY,
        GW_PERMISSION_CAN_DEBUG));
    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_FACTORY,
        GW_PERMISSION_NETWORK_CONFIG));
    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_FACTORY,
        GW_PERMISSION_WAREHOUSE_IDENTITY));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_FACTORY,
        GW_PERMISSION_MQTT_CONFIG | GW_PERMISSION_ETHERNET_CONFIG));

    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_TECH,
        GW_PERMISSION_WAREHOUSE_CONFIG));
    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_TECH,
        GW_PERMISSION_LASER_CONFIG | GW_PERMISSION_CAN_DEBUG | GW_PERMISSION_SYSTEM_DEBUG));
    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_TECH,
        GW_PERMISSION_NETWORK_CONFIG));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_TECH,
        GW_PERMISSION_WAREHOUSE_IDENTITY));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_TECH,
        GW_PERMISSION_MQTT_CONFIG | GW_PERMISSION_ETHERNET_CONFIG));

    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_IT,
        GW_PERMISSION_NETWORK_CONFIG | GW_PERMISSION_MQTT_CONFIG));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_IT,
        GW_PERMISSION_WAREHOUSE_IDENTITY));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_IT,
        GW_PERMISSION_LASER_CONFIG));
    TEST_ASSERT_FALSE(gateway_auth_role_has_permission(GW_ROLE_IT,
        GW_PERMISSION_CAN_DEBUG));

    const gateway_permission_t all = GW_PERMISSION_VIEW_PUBLIC |
        GW_PERMISSION_WAREHOUSE_CONFIG | GW_PERMISSION_LASER_CONFIG |
        GW_PERMISSION_CAN_DEBUG | GW_PERMISSION_SYSTEM_DEBUG |
        GW_PERMISSION_NETWORK_CONFIG | GW_PERMISSION_MQTT_CONFIG |
        GW_PERMISSION_ETHERNET_CONFIG | GW_PERMISSION_WAREHOUSE_IDENTITY;
    TEST_ASSERT_TRUE(gateway_auth_role_has_permission(GW_ROLE_SUPER_ADMIN, all));
}

TEST_CASE("gateway WiFi profiles promote the latest selection", "[gateway][wifi]")
{
    gateway_config_t config = {0};
    TEST_ASSERT_TRUE(gateway_config_add_wifi(&config, "WiFi-A", "pass-a"));
    TEST_ASSERT_TRUE(gateway_config_add_wifi(&config, "WiFi-B", "pass-b"));
    TEST_ASSERT_TRUE(gateway_config_add_wifi(&config, "WiFi-C", "pass-c"));
    TEST_ASSERT_EQUAL_STRING("WiFi-C", config.wifi_profiles[0].ssid);
    TEST_ASSERT_EQUAL_STRING("WiFi-B", config.wifi_profiles[1].ssid);
    TEST_ASSERT_EQUAL_STRING("WiFi-A", config.wifi_profiles[2].ssid);

    TEST_ASSERT_TRUE(gateway_config_add_wifi(&config, "WiFi-B", "pass-b-new"));
    TEST_ASSERT_EQUAL_UINT8(3, config.wifi_profile_count);
    TEST_ASSERT_EQUAL_STRING("WiFi-B", config.wifi_profiles[0].ssid);
    TEST_ASSERT_EQUAL_STRING("pass-b-new", config.wifi_profiles[0].password);
    TEST_ASSERT_EQUAL_STRING("WiFi-C", config.wifi_profiles[1].ssid);
    TEST_ASSERT_EQUAL_STRING("WiFi-A", config.wifi_profiles[2].ssid);

    TEST_ASSERT_TRUE(gateway_config_add_wifi(&config, "WiFi-D", "pass-d"));
    TEST_ASSERT_TRUE(gateway_config_add_wifi(&config, "WiFi-E", "pass-e"));
    TEST_ASSERT_TRUE(gateway_config_add_wifi(&config, "WiFi-F", "pass-f"));
    TEST_ASSERT_EQUAL_UINT8(GATEWAY_WIFI_PROFILE_MAX, config.wifi_profile_count);
    TEST_ASSERT_EQUAL_STRING("WiFi-F", config.wifi_profiles[0].ssid);
    TEST_ASSERT_EQUAL_STRING("WiFi-E", config.wifi_profiles[1].ssid);
    TEST_ASSERT_EQUAL_STRING("WiFi-D", config.wifi_profiles[2].ssid);
    TEST_ASSERT_EQUAL_STRING("WiFi-B", config.wifi_profiles[3].ssid);
    TEST_ASSERT_EQUAL_STRING("WiFi-C", config.wifi_profiles[4].ssid);
}

TEST_CASE("laser profile exact boundaries", "[gateway]")
{
    uint8_t group = 0;
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

TEST_CASE("local AP and Ethernet debug use separate fixed subnets", "[gateway]")
{
    TEST_ASSERT_EQUAL_STRING("192.168.65.204", GATEWAY_AP_IP);
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", GATEWAY_AP_NETMASK);
    TEST_ASSERT_EQUAL_STRING("192.168.66.204", GATEWAY_ETH_DEBUG_IP);
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", GATEWAY_ETH_DEBUG_NETMASK);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(GATEWAY_AP_IP, GATEWAY_ETH_DEBUG_IP));
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
        {.enabled=true,.position_id=2,.group_id=1,.laser_id=1,.warehouse_code="KHO-A01",
         .distance_mm=600,.distance_emergency_mm=300},
        {.enabled=true,.position_id=2,.group_id=2,.laser_id=11,.warehouse_code="KHO-A02",
         .distance_mm=600,.distance_emergency_mm=300},
    };
    warehouse_position_config_t candidate = {
        .enabled=true,.position_id=1,.group_id=1,.laser_id=1,.warehouse_code="KHO-B01",
        .distance_mm=600,.distance_emergency_mm=300,
    };
    TEST_ASSERT_EQUAL(WAREHOUSE_DUPLICATE_LASER_ID,
        warehouse_manager_validate_candidate(LASER_PROFILE_GROUP_12, positions, 2, &candidate));
    candidate.position_id = 2;
    candidate.group_id = 2;
    candidate.laser_id = 11;
    strlcpy(candidate.warehouse_code, "KHO-A01", sizeof(candidate.warehouse_code));
    positions[0].position_id = 1;
    positions[0].group_id = 1;
    positions[0].laser_id = 1;
    TEST_ASSERT_EQUAL(WAREHOUSE_DUPLICATE_CODE,
        warehouse_manager_validate_candidate(LASER_PROFILE_GROUP_12, positions, 2, &candidate));
    strlcpy(candidate.warehouse_code, "KHO-B02", sizeof(candidate.warehouse_code));
    candidate.distance_emergency_mm = 700;
    TEST_ASSERT_EQUAL(WAREHOUSE_INVALID_DISTANCE,
        warehouse_manager_validate_candidate(LASER_PROFILE_GROUP_12, positions, 2, &candidate));
}

TEST_CASE("warehouse commissioning is explicit and validation neutral",
          "[gateway][warehouse][laser-config]")
{
    const warehouse_position_config_t existing[] = {
        {
            .enabled = true,
            .position_id = 2,
            .group_id = 2,
            .laser_id = 11,
            .warehouse_code = "KHO-02",
            .distance_mm = 600,
            .distance_emergency_mm = 300,
            .low_col = 0x0FU,
            .high_row = 0xF0U,
            .proximity_enabled = true,
            .config_applied = true,
        },
    };
    warehouse_position_config_t draft = {
        .enabled = true,
        .position_id = 1,
        .group_id = 1,
        .laser_id = 2,
        .warehouse_code = "KHO-01",
        .distance_mm = 700,
        .distance_emergency_mm = 350,
        .low_col = 0x33U,
        .high_row = 0xCCU,
        .proximity_enabled = true,
        /* Saving a mapping must not implicitly mean that Apply was pressed. */
        .config_applied = false,
    };

    TEST_ASSERT_EQUAL(WAREHOUSE_VALID, warehouse_manager_validate_candidate(
        LASER_PROFILE_GROUP_12, existing, 1U, &draft));
    TEST_ASSERT_FALSE(draft.config_applied);

    draft.config_applied = true;
    TEST_ASSERT_EQUAL(WAREHOUSE_VALID, warehouse_manager_validate_candidate(
        LASER_PROFILE_GROUP_12, existing, 1U, &draft));
    TEST_ASSERT_TRUE(draft.config_applied);
}

TEST_CASE("restored Laser configuration table fails closed on invalid input",
          "[gateway][laser-config][restore]")
{
    const laser_can_config_request_t valid[] = {
        {
            .laser_id = 2,
            .distance_mm = 600,
            .distance_emergency_mm = 300,
            .low_col = 0x0FU,
            .high_row = 0xF0U,
            .proximity_enabled = true,
        },
        {
            .laser_id = 64,
            .distance_mm = 800,
            .distance_emergency_mm = 400,
            .low_col = 0x33U,
            .high_row = 0xCCU,
            .proximity_enabled = false,
        },
    };

    TEST_ASSERT_EQUAL(ESP_OK,
        laser_can_bringup_replace_configs(valid, 2U));
    /* Clear immediately: this test must never leave a later hardware test
     * able to answer a Laser configuration request. */
    TEST_ASSERT_EQUAL(ESP_OK,
        laser_can_bringup_replace_configs(NULL, 0U));

    laser_can_config_request_t duplicate_group[] = { valid[0], valid[0] };
    duplicate_group[1].laser_id = 10U;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        laser_can_bringup_replace_configs(duplicate_group, 2U));

    laser_can_config_request_t invalid = valid[0];
    invalid.distance_emergency_mm = invalid.distance_mm + 1U;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        laser_can_bringup_replace_configs(&invalid, 1U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        laser_can_bringup_replace_configs(NULL, 1U));
}

TEST_CASE("MQTT identity segment validator is strict", "[gateway][mqtt][topic]")
{
    TEST_ASSERT_TRUE(gateway_topic_segment_valid("aubot"));
    TEST_ASSERT_TRUE(gateway_topic_segment_valid("ha-noi_01"));
    TEST_ASSERT_TRUE(gateway_topic_segment_valid("0"));
    TEST_ASSERT_TRUE(gateway_topic_segment_valid("1234567890123456789012345678901"));

    TEST_ASSERT_FALSE(gateway_topic_segment_valid(NULL));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid(""));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("-aubot"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("_aubot"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("Aubot"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("ha noi"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("ha/noi"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("ha+noi"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("ha#noi"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("hà-nội"));
    TEST_ASSERT_FALSE(gateway_topic_segment_valid("12345678901234567890123456789012"));
}

TEST_CASE("Gateway ID deterministically owns warehouse identity", "[gateway][config][mqtt]")
{
    gateway_config_t config = mqtt_identity("legacy-kho", "Legacy Kho");
    strlcpy(config.gateway_id, "GW-01_A", sizeof(config.gateway_id));
    TEST_ASSERT_TRUE(gateway_config_gateway_id_valid(config.gateway_id));
    TEST_ASSERT_TRUE(gateway_config_derive_warehouse_identity(&config));
    TEST_ASSERT_EQUAL_STRING("gw-01_a", config.warehouse_id);
    TEST_ASSERT_EQUAL_STRING("GW-01_A", config.warehouse_name);

    gateway_topic_set_t topics = {0};
    TEST_ASSERT_EQUAL(ESP_OK, gateway_topic_build_set(&config, &topics));
    TEST_ASSERT_EQUAL_STRING(
        "warehouse/sensor/aubot/ha-noi/gw-01_a/status/json",
        topics.status_json);
}

TEST_CASE("Gateway ID rejects characters unsafe for automatic MQTT identity",
          "[gateway][config][mqtt]")
{
    TEST_ASSERT_TRUE(gateway_config_gateway_id_valid("GW-01"));
    TEST_ASSERT_TRUE(gateway_config_gateway_id_valid("1_gateway"));
    TEST_ASSERT_FALSE(gateway_config_gateway_id_valid(NULL));
    TEST_ASSERT_FALSE(gateway_config_gateway_id_valid(""));
    TEST_ASSERT_FALSE(gateway_config_gateway_id_valid("-GW-01"));
    TEST_ASSERT_FALSE(gateway_config_gateway_id_valid("_GW-01"));
    TEST_ASSERT_FALSE(gateway_config_gateway_id_valid("GW 01"));
    TEST_ASSERT_FALSE(gateway_config_gateway_id_valid("GW/01"));
    TEST_ASSERT_FALSE(gateway_config_gateway_id_valid("12345678901234567"));
}

TEST_CASE("automatic AP password stays secured for one-character Gateway ID",
          "[gateway][config][network]")
{
    char password[64] = {0};
    gateway_config_build_ap_identity("X", NULL, 0U, password,
                                     sizeof(password));
    TEST_ASSERT_EQUAL_STRING("AUBOT-X0", password);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(8U, strlen(password));
}

TEST_CASE("derived warehouse identity replaces legacy independent values",
          "[gateway][config][migration]")
{
    gateway_config_t config = mqtt_identity("kho-01", "Kho 01");
    strlcpy(config.gateway_id, "GW-01", sizeof(config.gateway_id));
    TEST_ASSERT_TRUE(gateway_config_derive_warehouse_identity(&config));
    TEST_ASSERT_EQUAL_STRING("gw-01", config.warehouse_id);
    TEST_ASSERT_EQUAL_STRING("GW-01", config.warehouse_name);
    /* Identity derivation is isolated from warehouse_v3 / position data. */
    TEST_ASSERT_EQUAL_STRING("aubot", config.company_id);
    TEST_ASSERT_EQUAL_STRING("ha-noi", config.site_id);
}

TEST_CASE("MQTT topics match the warehouse sensor namespace", "[gateway][mqtt][topic]")
{
    const gateway_config_t config = mqtt_identity("kho-vp", "Kho Văn Phòng");
    gateway_topic_set_t topics = {0};
    TEST_ASSERT_EQUAL(ESP_OK, gateway_topic_build_set(&config, &topics));
    TEST_ASSERT_EQUAL_STRING(
        "warehouse/sensor/aubot/ha-noi/kho-vp/status/json", topics.status_json);
    TEST_ASSERT_EQUAL_STRING(
        "warehouse/sensor/aubot/ha-noi/kho-vp/status/bits", topics.status_bits);
}

TEST_CASE("warehouse display rename keeps MQTT topics stable", "[gateway][mqtt][topic]")
{
    gateway_config_t before = mqtt_identity("kho-vp", "Kho Văn Phòng");
    gateway_config_t after = before;
    strlcpy(after.warehouse_name, "Kho Văn Phòng Mới", sizeof(after.warehouse_name));
    gateway_topic_set_t before_topics = {0}, after_topics = {0};
    TEST_ASSERT_EQUAL(ESP_OK, gateway_topic_build_set(&before, &before_topics));
    TEST_ASSERT_EQUAL(ESP_OK, gateway_topic_build_set(&after, &after_topics));
    TEST_ASSERT_EQUAL_MEMORY(&before_topics, &after_topics, sizeof(before_topics));
}

TEST_CASE("warehouse ID change moves every MQTT topic", "[gateway][mqtt][topic]")
{
    gateway_config_t before = mqtt_identity("kho-vp", "Kho Văn Phòng");
    gateway_config_t after = before;
    strlcpy(after.warehouse_id, "kho-vp-02", sizeof(after.warehouse_id));
    gateway_topic_set_t before_topics = {0}, after_topics = {0};
    TEST_ASSERT_EQUAL(ESP_OK, gateway_topic_build_set(&before, &before_topics));
    TEST_ASSERT_EQUAL(ESP_OK, gateway_topic_build_set(&after, &after_topics));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(before_topics.status_json, after_topics.status_json));
    TEST_ASSERT_EQUAL_STRING(
        "warehouse/sensor/aubot/ha-noi/kho-vp-02/status/json",
        after_topics.status_json);
    TEST_ASSERT_EQUAL_STRING(
        "warehouse/sensor/aubot/ha-noi/kho-vp-02/status/bits",
        after_topics.status_bits);
}

TEST_CASE("invalid MQTT identity cannot build topics", "[gateway][mqtt][topic]")
{
    gateway_config_t config = mqtt_identity("kho-vp", "Kho Văn Phòng");
    gateway_topic_set_t topics = {0};
    strlcpy(config.site_id, "Ha/Noi", sizeof(config.site_id));
    TEST_ASSERT_FALSE(gateway_identity_valid(&config));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gateway_topic_build_set(&config, &topics));

    config = mqtt_identity("kho-vp", "Kho Văn Phòng");
    config.warehouse_name[0] = '\0';
    TEST_ASSERT_FALSE(gateway_identity_valid(&config));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gateway_topic_build_set(&config, &topics));
}

TEST_CASE("MQTT two-bit codec covers all states in eight slots", "[gateway][mqtt][status]")
{
    warehouse_snapshot_t snapshot = status_snapshot(LASER_PROFILE_GROUP_12);
    configure_status_position(&snapshot, 0U, WAREHOUSE_STATE_EMPTY, true, true);
    configure_status_position(&snapshot, 1U, WAREHOUSE_STATE_OCCUPIED, true, true);
    /* Slot 3 is configured but offline: FAULT. */
    configure_status_position(&snapshot, 2U, WAREHOUSE_STATE_EMPTY, false, true);
    /* Slot 4 is online but has no valid status: UNKNOWN. */
    configure_status_position(&snapshot, 3U, WAREHOUSE_STATE_EMPTY, true, false);
    /* Slot 5 is disabled: UNKNOWN, even though stale runtime fields are set. */
    snapshot.positions[4].state = WAREHOUSE_STATE_OCCUPIED;
    snapshot.positions[4].sensor_online = true;
    snapshot.positions[4].status_valid = true;
    /* Slot 6 has an incomplete assignment: UNKNOWN. */
    snapshot.positions[5].config.enabled = true;
    snapshot.positions[5].config.group_id = 6U;

    char bits[GATEWAY_MQTT_STATUS_BITS_MAX + 1U];
    size_t bits_length = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, gateway_mqtt_status_bits(
        bits, sizeof(bits), &snapshot, &bits_length));
    TEST_ASSERT_EQUAL_size_t(16U, bits_length);
    TEST_ASSERT_EQUAL_STRING("0001111010101010", bits);
}

TEST_CASE("MQTT two-bit codec preserves all twelve slots", "[gateway][mqtt][status]")
{
    warehouse_snapshot_t snapshot = status_snapshot(LASER_PROFILE_GROUP_12);
    for (uint8_t i = 0U; i < snapshot.group_count; ++i) {
        const uint8_t state = i % 4U;
        if (state == 0U) {
            configure_status_position(&snapshot, i, WAREHOUSE_STATE_EMPTY, true, true);
        } else if (state == 1U) {
            configure_status_position(&snapshot, i, WAREHOUSE_STATE_OCCUPIED, true, true);
        } else if (state == 2U) {
            configure_status_position(&snapshot, i, WAREHOUSE_STATE_EMPTY, true, false);
        } else {
            configure_status_position(&snapshot, i, WAREHOUSE_STATE_EMPTY, false, true);
        }
    }
    char bits[GATEWAY_MQTT_STATUS_BITS_MAX + 1U];
    size_t bits_length = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, gateway_mqtt_status_bits(
        bits, sizeof(bits), &snapshot, &bits_length));
    TEST_ASSERT_EQUAL_size_t(24U, bits_length);
    TEST_ASSERT_EQUAL_STRING("000110110001101100011011", bits);
}

TEST_CASE("MQTT JSON and raw bits describe one identical snapshot", "[gateway][mqtt][status]")
{
    warehouse_snapshot_t snapshot = status_snapshot(LASER_PROFILE_GROUP_12);
    configure_status_position(&snapshot, 0U, WAREHOUSE_STATE_EMPTY, true, true);
    configure_status_position(&snapshot, 1U, WAREHOUSE_STATE_OCCUPIED, true, true);
    configure_status_position(&snapshot, 2U, WAREHOUSE_STATE_EMPTY, true, false);
    configure_status_position(&snapshot, 3U, WAREHOUSE_STATE_EMPTY, false, true);
    const gateway_mqtt_json_context_t context = mqtt_context(125U);
    char json[GATEWAY_MQTT_JSON_MAX];
    char bits[GATEWAY_MQTT_STATUS_BITS_MAX + 1U];
    size_t json_length = 0U, bits_length = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, gateway_mqtt_json_snapshot(
        json, sizeof(json), &context, &snapshot, &json_length));
    TEST_ASSERT_EQUAL(ESP_OK, gateway_mqtt_status_bits(
        bits, sizeof(bits), &snapshot, &bits_length));
    TEST_ASSERT_EQUAL_size_t(strlen(json), json_length);
    TEST_ASSERT_EQUAL_size_t(16U, bits_length);
    assert_json_state_bits(json, bits);
    assert_status_json_field_order(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"schema\":\"WAREHOUSE_STATUS_V1\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"company_id\":\"aubot\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"site_id\":\"ha-noi\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"warehouse_id\":\"kho-vp\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"warehouse_name\":\"Kho Văn Phòng\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"source_type\":\"sensor\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"slot_count\":8"));
    TEST_ASSERT_NOT_NULL(strstr(json,
        "\"states\":[\"EMPTY\",\"OCCUPIED\",\"UNKNOWN\",\"FAULT\","));
    TEST_ASSERT_NOT_NULL(strstr(json,
        "\"occupied_count\":1,\"empty_count\":1,\"unknown_count\":5,\"fault_count\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sequence\":125"));
    TEST_ASSERT_NOT_NULL(strstr(json,
        "\"generated_at\":\"2026-08-12T02:20:30.000000Z\""));
    TEST_ASSERT_NULL(strstr(json, "\"gateway_id\""));
    TEST_ASSERT_NULL(strstr(json, "\"boot_id\""));
    TEST_ASSERT_NULL(strstr(json, "\"profile\""));
}

TEST_CASE("MQTT status rejects invalid snapshot shape and short buffers", "[gateway][mqtt][status]")
{
    warehouse_snapshot_t invalid = status_snapshot(LASER_PROFILE_GROUP_12);
    invalid.group_count = 7U;
    char bits[GATEWAY_MQTT_STATUS_BITS_MAX + 1U];
    size_t length = 99U;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gateway_mqtt_status_bits(
        bits, sizeof(bits), &invalid, &length));
    TEST_ASSERT_EQUAL_size_t(0U, length);

    warehouse_snapshot_t snapshot = status_snapshot(LASER_PROFILE_GROUP_12);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, gateway_mqtt_status_bits(
        bits, 15U, &snapshot, &length));
    TEST_ASSERT_EQUAL_size_t(0U, length);
}
