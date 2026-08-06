#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_boot_button_init(void);
esp_err_t bsp_boot_button_is_pressed(bool *pressed);

#ifdef __cplusplus
}
#endif
