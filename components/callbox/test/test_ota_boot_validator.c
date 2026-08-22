#include "unity.h"
#include "ota_boot_validator_private.h"

TEST_CASE("normal boot qualification accepts periodic local task progress", "[ota_boot]")
{
    uint32_t before[HEALTH_TASK_COUNT] = { 0 }, after[HEALTH_TASK_COUNT] = { 0 };
    for (int i = 0; i < HEALTH_TASK_COUNT; ++i) after[i] = 1;
    TEST_ASSERT_TRUE(ota_boot_validator_normal_tasks_progressed(before, after));
}
TEST_CASE("normal boot qualification rejects a missing required heartbeat", "[ota_boot]")
{
    uint32_t before[HEALTH_TASK_COUNT] = { 0 }, after[HEALTH_TASK_COUNT] = { 0 };
    for (int i = 0; i < HEALTH_TASK_COUNT; ++i) after[i] = 1;
    after[HEALTH_TASK_MQTT_TX] = 0;
    TEST_ASSERT_FALSE(ota_boot_validator_normal_tasks_progressed(before, after));
}
