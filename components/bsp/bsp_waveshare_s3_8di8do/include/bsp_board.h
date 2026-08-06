#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_buzzer;
    bool has_rgb;
    uint8_t digital_input_count;
    uint8_t digital_output_count;
} bsp_capabilities_t;

esp_err_t bsp_board_init(void);
const bsp_capabilities_t *bsp_board_get_capabilities(void);

#ifdef __cplusplus
}
#endif
