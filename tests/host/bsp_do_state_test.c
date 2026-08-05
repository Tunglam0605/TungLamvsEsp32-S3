#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "bsp_do_state.h"

static void test_active_high_state_tracking(void)
{
    bsp_do_state_t state;
    bsp_do_state_init(&state, true, 0x00);

    assert(state.desired_mask == 0x00);
    assert(state.safe_mask == 0x00);
    assert(state.applied_mask == 0x00);
    assert(bsp_do_state_to_register_mask(&state, 0x00) == 0x00);

    bsp_do_state_set_desired(&state, 0xA5);
    assert(state.desired_mask == 0xA5);
    // A requested write must not be reported as applied before I2C succeeds.
    assert(state.applied_mask == 0x00);
    assert(bsp_do_state_to_register_mask(&state, state.desired_mask) == 0xA5);

    bsp_do_state_commit_applied(&state, state.desired_mask);
    assert(state.applied_mask == 0xA5);
}

static void test_active_low_translation(void)
{
    bsp_do_state_t state;
    bsp_do_state_init(&state, false, 0x00);

    // Logical OFF is not assumed to be register-low when polarity is active-low.
    assert(bsp_do_state_to_register_mask(&state, 0x00) == UINT8_MAX);

    bsp_do_state_set_desired(&state, 0x3C);
    assert(state.applied_mask == 0x00);
    assert(bsp_do_state_to_register_mask(&state, state.desired_mask) == 0xC3);

    bsp_do_state_commit_applied(&state, state.desired_mask);
    assert(state.applied_mask == 0x3C);
}

int main(void)
{
    test_active_high_state_tracking();
    test_active_low_translation();
    puts("bsp_do_state_test: PASS");
    return 0;
}
