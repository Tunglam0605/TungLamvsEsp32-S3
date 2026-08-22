#include "unity.h"

#include "cancel_hold_gesture.h"

static void begin(cancel_hold_gesture_t *gesture)
{
    cancel_hold_gesture_reset(gesture);
    cancel_hold_gesture_press(gesture, 100U);
}

TEST_CASE("short Cancel release creates normal cancel intent", "[callbox][cancel_hold]")
{
    cancel_hold_gesture_t gesture;
    begin(&gesture);
    TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_CANCEL,
                             cancel_hold_gesture_release(&gesture, 5099U));
}

TEST_CASE("five second Cancel hold emits Rescue AP once and consumes cancel", "[callbox][cancel_hold]")
{
    cancel_hold_gesture_t gesture;
    begin(&gesture);
    TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_RESCUE_AP,
                             cancel_hold_gesture_tick(&gesture, 5100U, true));
    TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_NONE,
                             cancel_hold_gesture_tick(&gesture, 6000U, true));
    TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_NONE,
                             cancel_hold_gesture_release(&gesture, 6000U));
}

TEST_CASE("six through ten second warnings emit exactly once", "[callbox][cancel_hold]")
{
    cancel_hold_gesture_t gesture;
    begin(&gesture);
    (void)cancel_hold_gesture_tick(&gesture, 5100U, true);
    const uint32_t warnings[] = {
        CANCEL_HOLD_ACTION_WARNING_6S, CANCEL_HOLD_ACTION_WARNING_7S,
        CANCEL_HOLD_ACTION_WARNING_8S, CANCEL_HOLD_ACTION_WARNING_9S,
        CANCEL_HOLD_ACTION_WARNING_10S,
    };
    for (unsigned second = 6U; second <= 10U; ++second) {
        const uint32_t actions = cancel_hold_gesture_tick(&gesture, second * 1000U + 100U, true);
        TEST_ASSERT_TRUE(actions & warnings[second - 6U]);
        TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_NONE,
                                 cancel_hold_gesture_tick(&gesture, second * 1000U + 101U, true));
    }
}

TEST_CASE("OTA request is emitted exactly once at ten seconds", "[callbox][cancel_hold]")
{
    cancel_hold_gesture_t gesture;
    begin(&gesture);
    const uint32_t before_ten = cancel_hold_gesture_tick(&gesture, 10099U, true);
    TEST_ASSERT_FALSE(before_ten & CANCEL_HOLD_ACTION_OTA_REQUEST);
    const uint32_t actions = cancel_hold_gesture_tick(&gesture, 10100U, true);
    TEST_ASSERT_TRUE(actions & CANCEL_HOLD_ACTION_OTA_REQUEST);
    TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_NONE,
                             cancel_hold_gesture_tick(&gesture, 11000U, true));
}

TEST_CASE("release flushes crossed milestones and never emits long-hold cancel", "[callbox][cancel_hold]")
{
    cancel_hold_gesture_t gesture;
    begin(&gesture);
    const uint32_t at_five = cancel_hold_gesture_release(&gesture, 5100U);
    TEST_ASSERT_TRUE(at_five & CANCEL_HOLD_ACTION_RESCUE_AP);
    TEST_ASSERT_FALSE(at_five & CANCEL_HOLD_ACTION_CANCEL);

    begin(&gesture);
    const uint32_t at_ten = cancel_hold_gesture_release(&gesture, 10100U);
    TEST_ASSERT_TRUE(at_ten & CANCEL_HOLD_ACTION_RESCUE_AP);
    TEST_ASSERT_TRUE(at_ten & CANCEL_HOLD_ACTION_WARNING_6S);
    TEST_ASSERT_TRUE(at_ten & CANCEL_HOLD_ACTION_WARNING_7S);
    TEST_ASSERT_TRUE(at_ten & CANCEL_HOLD_ACTION_WARNING_8S);
    TEST_ASSERT_TRUE(at_ten & CANCEL_HOLD_ACTION_WARNING_9S);
    TEST_ASSERT_TRUE(at_ten & CANCEL_HOLD_ACTION_WARNING_10S);
    TEST_ASSERT_TRUE(at_ten & CANCEL_HOLD_ACTION_OTA_REQUEST);
    TEST_ASSERT_FALSE(at_ten & CANCEL_HOLD_ACTION_CANCEL);
}

TEST_CASE("release and repress reset Cancel hold milestones", "[callbox][cancel_hold]")
{
    cancel_hold_gesture_t gesture;
    begin(&gesture);
    (void)cancel_hold_gesture_tick(&gesture, 6100U, true);
    (void)cancel_hold_gesture_release(&gesture, 6200U);
    cancel_hold_gesture_press(&gesture, 7000U);
    TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_NONE,
                             cancel_hold_gesture_tick(&gesture, 11999U, true));
    TEST_ASSERT_EQUAL_UINT32(CANCEL_HOLD_ACTION_RESCUE_AP,
                             cancel_hold_gesture_tick(&gesture, 12000U, true));
}

TEST_CASE("failed OTA dispatch cannot repeat while same hold continues", "[callbox][cancel_hold]")
{
    cancel_hold_gesture_t gesture;
    begin(&gesture);
    const uint32_t first = cancel_hold_gesture_tick(&gesture, 10100U, true);
    TEST_ASSERT_TRUE(first & CANCEL_HOLD_ACTION_OTA_REQUEST);
    /* The production caller may fail the request; no acknowledgement feeds
     * back into the recognizer, so this proves it cannot retry the hold. */
    TEST_ASSERT_FALSE(cancel_hold_gesture_tick(&gesture, 10200U, true) &
                      CANCEL_HOLD_ACTION_OTA_REQUEST);
}
