#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_DO_1 = 0,
    BSP_DO_2,
    BSP_DO_3,
    BSP_DO_4,
    BSP_DO_5,
    BSP_DO_6,
    BSP_DO_7,
    BSP_DO_8,
    BSP_DO_COUNT,
} bsp_do_channel_t;

typedef struct {
    uint8_t desired_mask;
    uint8_t applied_mask;
    uint8_t safe_mask;
    bool applied_valid;
} bsp_do_status_t;

esp_err_t bsp_do_init(void);
esp_err_t bsp_do_write(bsp_do_channel_t channel, bool state);
esp_err_t bsp_do_write_mask(uint8_t mask);
esp_err_t bsp_do_get_status(bsp_do_status_t *status);
esp_err_t bsp_do_apply_safe_state(void);
bool bsp_do_uses_provisional_active_high(void);

#ifdef __cplusplus
}
#endif
