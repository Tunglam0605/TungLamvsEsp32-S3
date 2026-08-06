#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_DI_1 = 0,
    BSP_DI_2,
    BSP_DI_3,
    BSP_DI_4,
    BSP_DI_5,
    BSP_DI_6,
    BSP_DI_7,
    BSP_DI_8,
    BSP_DI_COUNT,
} bsp_di_channel_t;

esp_err_t bsp_di_init(void);
esp_err_t bsp_di_read_raw(bsp_di_channel_t channel, bool *raw_high);
esp_err_t bsp_di_read_raw_mask(uint8_t *raw_high_mask);
esp_err_t bsp_di_read(bsp_di_channel_t channel, bool *active);
esp_err_t bsp_di_read_mask(uint8_t *active_mask);
bool bsp_di_uses_provisional_active_low(void);

#ifdef __cplusplus
}
#endif
