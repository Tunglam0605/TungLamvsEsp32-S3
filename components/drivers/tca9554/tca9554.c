/**
 * @file    tca9554.c
 * @brief   Generic TCA9554/PCA9554 8-bit I2C I/O expander driver.
 *
 *          Implements the register operations exposed by tca9554.h.
 *          The register map is an implementation detail and stays private
 *          to this translation unit.
 *
 * @note    Thread safety: the driver is NOT thread-safe. It keeps instance
 *          state (bus/device handle, address, timeout) but does not create
 *          a mutex. The caller owns serialization of all I2C transactions
 *          (e.g. BSP_DO serializes read-modify-write of the OUTPUT register).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     tca9554.h — public driver API
 */
#include "tca9554.h"

#include "esp_log.h"

static const char *TAG = "TCA9554";

/* Register map — implementation detail, not exposed publicly.
 * CONFIG bit = 1 → input, bit = 0 → output. */
#define TCA9554_REG_INPUT    0x00
#define TCA9554_REG_OUTPUT   0x01
#define TCA9554_REG_POLARITY 0x02
#define TCA9554_REG_CONFIG   0x03

/* A stalled SDA/SCL line must return an error, never block the application
 * forever.  Normal two-byte TCA9554 writes complete in a few milliseconds;
 * the value itself comes from the instance config. */
#define TCA9554_MS_TIMEOUT_FALLBACK 100U

static bool tca9554_pin_valid(uint8_t pin)
{
    return pin < TCA9554_PIN_COUNT;
}

static esp_err_t tca9554_write_byte(tca9554_t *dev, uint8_t reg, uint8_t value)
{
    if (!dev || !dev->dev) return ESP_ERR_INVALID_ARG;
    const uint8_t frame[2] = { reg, value };
    return i2c_master_transmit(dev->dev, frame, sizeof(frame), dev->timeout_ms);
}

static esp_err_t tca9554_read_byte(tca9554_t *dev, uint8_t reg, uint8_t *value)
{
    if (!dev || !dev->dev || !value) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(dev->dev, &reg, 1, value, 1, dev->timeout_ms);
}

esp_err_t tca9554_init(tca9554_t *dev, const tca9554_config_t *config,
                       uint8_t initial_outputs)
{
    if (!dev || !config || !config->bus || config->address == 0 ||
        config->clock_hz == 0 || config->timeout_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->bus = config->bus;
    dev->timeout_ms = config->timeout_ms;

    i2c_device_config_t device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = config->address,
        .scl_speed_hz    = config->clock_hz,
    };
    esp_err_t ret = i2c_master_bus_add_device(config->bus, &device_cfg, &dev->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TCA9554 device at 0x%02X: %s",
                 config->address, esp_err_to_name(ret));
        return ret;
    }

    /* Direction: all pins as outputs (CONFIG = 0x00). */
    ret = tca9554_write_byte(dev, TCA9554_REG_CONFIG, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure TCA9554 outputs: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev->dev);
        dev->dev = NULL;
        return ret;
    }

    /* Initial raw electrical output byte (e.g. 0xFF = all inactive for
     * active-low outputs). */
    ret = tca9554_write_byte(dev, TCA9554_REG_OUTPUT, initial_outputs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init TCA9554 outputs: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev->dev);
        dev->dev = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "TCA9554 ready (addr=0x%02X, %lu Hz)", config->address,
             (unsigned long)config->clock_hz);
    return ESP_OK;
}

esp_err_t tca9554_set_all_outputs(tca9554_t *dev)
{
    if (!dev || !dev->dev) return ESP_ERR_INVALID_ARG;
    return tca9554_write_byte(dev, TCA9554_REG_CONFIG, 0x00);
}

esp_err_t tca9554_set_pin_mode(tca9554_t *dev, uint8_t pin, bool input)
{
    if (!dev || !dev->dev || !tca9554_pin_valid(pin)) return ESP_ERR_INVALID_ARG;

    uint8_t cfg = 0;
    esp_err_t ret = tca9554_read_byte(dev, TCA9554_REG_CONFIG, &cfg);
    if (ret != ESP_OK) return ret;

    if (input) cfg |= (1u << pin);
    else cfg &= ~(1u << pin);

    return tca9554_write_byte(dev, TCA9554_REG_CONFIG, cfg);
}

esp_err_t tca9554_read_outputs(tca9554_t *dev, uint8_t *out)
{
    if (!dev || !dev->dev || !out) return ESP_ERR_INVALID_ARG;
    return tca9554_read_byte(dev, TCA9554_REG_OUTPUT, out);
}

esp_err_t tca9554_write_outputs(tca9554_t *dev, uint8_t outputs)
{
    if (!dev || !dev->dev) return ESP_ERR_INVALID_ARG;
    return tca9554_write_byte(dev, TCA9554_REG_OUTPUT, outputs);
}

esp_err_t tca9554_write_pin(tca9554_t *dev, uint8_t pin, bool level)
{
    if (!dev || !dev->dev || !tca9554_pin_valid(pin)) return ESP_ERR_INVALID_ARG;

    uint8_t out = 0;
    esp_err_t ret = tca9554_read_outputs(dev, &out);
    if (ret != ESP_OK) return ret;

    if (level) out |= (1u << pin);
    else out &= ~(1u << pin);

    return tca9554_write_outputs(dev, out);
}

esp_err_t tca9554_toggle_pin(tca9554_t *dev, uint8_t pin)
{
    if (!dev || !dev->dev || !tca9554_pin_valid(pin)) return ESP_ERR_INVALID_ARG;

    uint8_t out = 0;
    esp_err_t ret = tca9554_read_outputs(dev, &out);
    if (ret != ESP_OK) return ret;

    out ^= (1u << pin);
    return tca9554_write_outputs(dev, out);
}
