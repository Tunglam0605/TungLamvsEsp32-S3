#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool logical_on_is_register_high;
    uint8_t desired_mask;
    uint8_t applied_mask;
    uint8_t safe_mask;
} bsp_do_state_t;

void bsp_do_state_init(bsp_do_state_t *state, bool logical_on_is_register_high, uint8_t safe_mask);
uint8_t bsp_do_state_to_register_mask(const bsp_do_state_t *state, uint8_t logical_mask);
void bsp_do_state_set_desired(bsp_do_state_t *state, uint8_t logical_mask);
void bsp_do_state_commit_applied(bsp_do_state_t *state, uint8_t logical_mask);
