#ifndef CALLBOX_CANCEL_HOLD_GESTURE_H
#define CALLBOX_CANCEL_HOLD_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

/* Pure Cancel-button hold recognizer.  The caller owns all side effects. */
typedef struct {
    bool active;
    uint32_t started_ms;
    uint8_t emitted_milestones;
} cancel_hold_gesture_t;

typedef enum {
    CANCEL_HOLD_ACTION_NONE       = 0U,
    CANCEL_HOLD_ACTION_CANCEL     = 1U << 0,
    CANCEL_HOLD_ACTION_RESCUE_AP  = 1U << 1,
    CANCEL_HOLD_ACTION_WARNING_6S = 1U << 2,
    CANCEL_HOLD_ACTION_WARNING_7S = 1U << 3,
    CANCEL_HOLD_ACTION_WARNING_8S = 1U << 4,
    CANCEL_HOLD_ACTION_WARNING_9S = 1U << 5,
    CANCEL_HOLD_ACTION_WARNING_10S = 1U << 6,
    CANCEL_HOLD_ACTION_OTA_REQUEST = 1U << 7,
} cancel_hold_action_t;

void cancel_hold_gesture_reset(cancel_hold_gesture_t *gesture);
void cancel_hold_gesture_press(cancel_hold_gesture_t *gesture, uint32_t now_ms);
/* Returns CANCEL only for a release before the five-second milestone. */
uint32_t cancel_hold_gesture_release(cancel_hold_gesture_t *gesture, uint32_t now_ms);
/* Returns each due milestone once while the physical button remains held. */
uint32_t cancel_hold_gesture_tick(cancel_hold_gesture_t *gesture, uint32_t now_ms,
                                  bool button_held);

#endif /* CALLBOX_CANCEL_HOLD_GESTURE_H */
