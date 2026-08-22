#include "unity.h"
#include "ota_policy_private.h"

static callbox_status_t idle_ready(void)
{
    callbox_status_t s = {0};
    s.Mission[0] = TASK_IDLE;
    s.Mission[1] = TASK_IDLE;
    s.CommState = COMM_READY;
    return s;
}

static ota_status_t ota_in(ota_state_t state)
{
    ota_status_t s = {0};
    s.state = state;
    return s;
}

TEST_CASE("normal OTA policy accepts authenticated idle ready CallBox", "[callbox][ota][policy]")
{
    callbox_status_t c = idle_ready();
    ota_status_t o = ota_in(OTA_STATE_IDLE);
    ota_policy_decision_t d;
    TEST_ASSERT_EQUAL(ESP_OK, ota_policy_evaluate_snapshot(OTA_POLICY_MODE_NORMAL,
        OTA_POLICY_SOURCE_WEB_LOCAL, OTA_POLICY_ACTION_BEGIN, true, &c, &o, &d));
    TEST_ASSERT_TRUE(d.allowed);
}

TEST_CASE("normal OTA policy rejects active mission and unsafe communication", "[callbox][ota][policy]")
{
    callbox_status_t c = idle_ready();
    ota_status_t o = ota_in(OTA_STATE_IDLE);
    ota_policy_decision_t d;
    c.Mission[0] = TASK_ASSIGNED;
    TEST_ASSERT_EQUAL(ESP_OK, ota_policy_evaluate_snapshot(OTA_POLICY_MODE_NORMAL,
        OTA_POLICY_SOURCE_WEB_LOCAL, OTA_POLICY_ACTION_BEGIN, true, &c, &o, &d));
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL(OTA_POLICY_DENY_MISSION_ACTIVE, d.reason);
    c.Mission[0] = TASK_IDLE;
    c.CommState = COMM_SYNCING;
    TEST_ASSERT_EQUAL(ESP_OK, ota_policy_evaluate_snapshot(OTA_POLICY_MODE_NORMAL,
        OTA_POLICY_SOURCE_INTERNAL_TRIGGER, OTA_POLICY_ACTION_BEGIN, false, &c, &o, &d));
    TEST_ASSERT_EQUAL(OTA_POLICY_DENY_COMMUNICATION, d.reason);
}

TEST_CASE("recovery OTA policy is authenticated local only and ignores WCS", "[callbox][ota][policy]")
{
    callbox_status_t c = {0};
    ota_status_t o = ota_in(OTA_STATE_IDLE);
    ota_policy_decision_t d;
    TEST_ASSERT_EQUAL(ESP_OK, ota_policy_evaluate_snapshot(OTA_POLICY_MODE_RECOVERY,
        OTA_POLICY_SOURCE_WEB_LOCAL, OTA_POLICY_ACTION_BEGIN, true, &c, &o, &d));
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_EQUAL(ESP_OK, ota_policy_evaluate_snapshot(OTA_POLICY_MODE_RECOVERY,
        OTA_POLICY_SOURCE_INTERNAL_TRIGGER, OTA_POLICY_ACTION_BEGIN, true, &c, &o, &d));
    TEST_ASSERT_EQUAL(OTA_POLICY_DENY_SOURCE, d.reason);
}

TEST_CASE("OTA policy enforces transaction state per action", "[callbox][ota][policy]")
{
    callbox_status_t c = idle_ready();
    ota_policy_decision_t d;
    ota_status_t o = ota_in(OTA_STATE_STAGED);
    TEST_ASSERT_EQUAL(ESP_OK, ota_policy_evaluate_snapshot(OTA_POLICY_MODE_NORMAL,
        OTA_POLICY_SOURCE_WEB_LOCAL, OTA_POLICY_ACTION_INSTALL, true, &c, &o, &d));
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_EQUAL(ESP_OK, ota_policy_evaluate_snapshot(OTA_POLICY_MODE_NORMAL,
        OTA_POLICY_SOURCE_WEB_LOCAL, OTA_POLICY_ACTION_BEGIN, true, &c, &o, &d));
    TEST_ASSERT_EQUAL(OTA_POLICY_DENY_OTA_STATE, d.reason);
}
