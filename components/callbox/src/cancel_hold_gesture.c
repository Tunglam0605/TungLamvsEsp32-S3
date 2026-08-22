#include "cancel_hold_gesture.h"

#define CANCEL_RESCUE_HOLD_MS 5000U

enum {
    MILESTONE_RESCUE = 1U << 0,
    MILESTONE_WARNING_6S = 1U << 1,
    MILESTONE_WARNING_7S = 1U << 2,
    MILESTONE_WARNING_8S = 1U << 3,
    MILESTONE_WARNING_9S = 1U << 4,
    MILESTONE_WARNING_10S = 1U << 5,
};

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

void cancel_hold_gesture_reset(cancel_hold_gesture_t *gesture)
{
    if (!gesture) return;
    gesture->active = false;
    gesture->started_ms = 0U;
    gesture->emitted_milestones = 0U;
}

void cancel_hold_gesture_press(cancel_hold_gesture_t *gesture, uint32_t now_ms)
{
    if (!gesture) return;
    gesture->active = true;
    gesture->started_ms = now_ms;
    gesture->emitted_milestones = 0U;
}

uint32_t cancel_hold_gesture_release(cancel_hold_gesture_t *gesture, uint32_t now_ms)
{
    if (!gesture || !gesture->active) return CANCEL_HOLD_ACTION_NONE;

    /* Flush any milestone crossed since the last scheduler tick before the
     * release edge resets state. This closes the 20 ms Mission-loop race at
     * exactly 5 s / 10 s without changing one-shot semantics. */
    uint32_t actions = cancel_hold_gesture_tick(gesture, now_ms, true);
    if (!time_reached(now_ms, gesture->started_ms + CANCEL_RESCUE_HOLD_MS)) {
        actions |= CANCEL_HOLD_ACTION_CANCEL;
    }
    cancel_hold_gesture_reset(gesture);
    return actions;
}

uint32_t cancel_hold_gesture_tick(cancel_hold_gesture_t *gesture, uint32_t now_ms,
                                  bool button_held)
{
    if (!gesture || !gesture->active || !button_held) return CANCEL_HOLD_ACTION_NONE;

    uint32_t actions = CANCEL_HOLD_ACTION_NONE;
    const uint32_t elapsed_ms = now_ms - gesture->started_ms;
    const struct {
        uint32_t at_ms;
        uint8_t milestone;
        uint32_t action;
    } milestones[] = {
        {5000U, MILESTONE_RESCUE, CANCEL_HOLD_ACTION_RESCUE_AP},
        {6000U, MILESTONE_WARNING_6S, CANCEL_HOLD_ACTION_WARNING_6S},
        {7000U, MILESTONE_WARNING_7S, CANCEL_HOLD_ACTION_WARNING_7S},
        {8000U, MILESTONE_WARNING_8S, CANCEL_HOLD_ACTION_WARNING_8S},
        {9000U, MILESTONE_WARNING_9S, CANCEL_HOLD_ACTION_WARNING_9S},
        {10000U, MILESTONE_WARNING_10S,
         CANCEL_HOLD_ACTION_WARNING_10S | CANCEL_HOLD_ACTION_OTA_REQUEST},
    };
    for (unsigned i = 0; i < sizeof(milestones) / sizeof(milestones[0]); ++i) {
        if (elapsed_ms >= milestones[i].at_ms &&
            !(gesture->emitted_milestones & milestones[i].milestone)) {
            gesture->emitted_milestones |= milestones[i].milestone;
            actions |= milestones[i].action;
        }
    }
    return actions;
}
