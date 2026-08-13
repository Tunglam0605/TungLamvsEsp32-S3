#pragma once

#include "bsp_types.h"

typedef struct {
    bsp_do_channel_t buzzer;
    bsp_do_channel_t tower_red;
    bsp_do_channel_t tower_yellow;
    bsp_do_channel_t tower_green;
    bsp_do_channel_t ap_status;
} gateway_io_mapping_t;

const gateway_io_mapping_t *gateway_io_get_mapping(void);
