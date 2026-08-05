#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tca9554_device *tca9554_handle_t;

typedef struct {
    i2c_master_bus_handle_t bus;
    uint8_t address;
    uint32_t timeout_ms;
} tca9554_config_t;

esp_err_t tca9554_create(const tca9554_config_t *config, tca9554_handle_t *out_handle);
esp_err_t tca9554_set_direction(tca9554_handle_t handle, uint8_t direction_mask);
esp_err_t tca9554_write_outputs(tca9554_handle_t handle, uint8_t output_mask);
esp_err_t tca9554_read_inputs(tca9554_handle_t handle, uint8_t *input_mask);
esp_err_t tca9554_delete(tca9554_handle_t handle);

#ifdef __cplusplus
}
#endif
