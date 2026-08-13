#include "gateway_io.h"

/* Waveshare ESP32-S3 POE ETH 8DI/8DO production wiring.
 * DO8 is the local configuration-AP indicator, matching Callbox. */
static const gateway_io_mapping_t s_mapping = {
    .buzzer = BSP_DO_1,
    .tower_red = BSP_DO_2,
    .tower_yellow = BSP_DO_3,
    .tower_green = BSP_DO_4,
    .ap_status = BSP_DO_8,
};

const gateway_io_mapping_t *gateway_io_get_mapping(void)
{
    return &s_mapping;
}
