#pragma once
#include <stdbool.h>
#include "bsp_types.h"
#include "esp_err.h"

typedef struct {
    bool mapping_confirmed;
    bsp_do_channel_t red;
    bsp_do_channel_t yellow;
    bsp_do_channel_t green;
} gateway_indicator_config_t;

esp_err_t gateway_indicator_start(const gateway_indicator_config_t *config);
