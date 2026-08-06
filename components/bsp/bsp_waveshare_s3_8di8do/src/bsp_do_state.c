#include "bsp_do_state.h"

void bsp_do_state_init(bsp_do_state_t *state, bool logical_on_is_register_high, uint8_t safe_mask)
{
    state->logical_on_is_register_high = logical_on_is_register_high;
    state->safe_mask = safe_mask;
    state->desired_mask = safe_mask;
    // Applied state becomes valid only when BSP commits a successful I2C write.
    state->applied_mask = 0;
    state->applied_valid = false;
}

uint8_t bsp_do_state_to_register_mask(const bsp_do_state_t *state, uint8_t logical_mask)
{
    return state->logical_on_is_register_high ? logical_mask : (uint8_t)~logical_mask;
}

void bsp_do_state_set_desired(bsp_do_state_t *state, uint8_t logical_mask)
{
    state->desired_mask = logical_mask;
}

void bsp_do_state_commit_applied(bsp_do_state_t *state, uint8_t logical_mask)
{
    state->applied_mask = logical_mask;
    state->applied_valid = true;
}
