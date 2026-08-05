#include "tca9554.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "tca9554_private.h"

struct tca9554_device {
    i2c_master_dev_handle_t device;
    SemaphoreHandle_t lock;
    uint32_t timeout_ms;
};

static esp_err_t tca9554_lock(tca9554_handle_t handle)
{
    if (xSemaphoreTake(handle->lock, pdMS_TO_TICKS(handle->timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void tca9554_unlock(tca9554_handle_t handle)
{
    xSemaphoreGive(handle->lock);
}

static esp_err_t tca9554_write_register(tca9554_handle_t handle, uint8_t reg, uint8_t value)
{
    const uint8_t tx_buffer[] = {reg, value};
    esp_err_t err = tca9554_lock(handle);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit(handle->device, tx_buffer, sizeof(tx_buffer), handle->timeout_ms);
    tca9554_unlock(handle);
    return err;
}

esp_err_t tca9554_create(const tca9554_config_t *config, tca9554_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || config->bus == NULL || config->address > 0x7f) {
        return ESP_ERR_INVALID_ARG;
    }

    tca9554_handle_t handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->timeout_ms = config->timeout_ms == 0 ? TCA9554_DEFAULT_TIMEOUT_MS : config->timeout_ms;
    handle->lock = xSemaphoreCreateMutex();
    if (handle->lock == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->address,
        .scl_speed_hz = TCA9554_DEFAULT_I2C_SPEED_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(config->bus, &device_config, &handle->device);
    if (err != ESP_OK) {
        vSemaphoreDelete(handle->lock);
        free(handle);
        return err;
    }

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t tca9554_set_direction(tca9554_handle_t handle, uint8_t direction_mask)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return tca9554_write_register(handle, TCA9554_REG_CONFIGURATION, direction_mask);
}

esp_err_t tca9554_write_outputs(tca9554_handle_t handle, uint8_t output_mask)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return tca9554_write_register(handle, TCA9554_REG_OUTPUT_PORT, output_mask);
}

esp_err_t tca9554_read_inputs(tca9554_handle_t handle, uint8_t *input_mask)
{
    if (handle == NULL || input_mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t register_address = TCA9554_REG_INPUT_PORT;
    esp_err_t err = tca9554_lock(handle);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit_receive(handle->device,
                                      &register_address,
                                      sizeof(register_address),
                                      input_mask,
                                      sizeof(*input_mask),
                                      handle->timeout_ms);
    tca9554_unlock(handle);
    return err;
}

esp_err_t tca9554_delete(tca9554_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_master_bus_rm_device(handle->device);
    if (err != ESP_OK) {
        return err;
    }

    vSemaphoreDelete(handle->lock);
    free(handle);
    return ESP_OK;
}
