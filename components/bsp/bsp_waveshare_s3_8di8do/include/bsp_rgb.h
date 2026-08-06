#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_rgb_init(void);
esp_err_t bsp_rgb_set(uint8_t red, uint8_t green, uint8_t blue);

#ifdef __cplusplus
}
#endif
