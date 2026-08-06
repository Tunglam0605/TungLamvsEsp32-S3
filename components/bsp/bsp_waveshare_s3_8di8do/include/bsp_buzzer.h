#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_buzzer_init(void);
esp_err_t bsp_buzzer_set(bool enabled);

#ifdef __cplusplus
}
#endif
