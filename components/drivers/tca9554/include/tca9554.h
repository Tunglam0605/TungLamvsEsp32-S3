/**
 * @file    tca9554.h
 * @brief   Generic device driver for the TCA9554/PCA9554 8-bit I2C I/O
 *          expander (GPIO over I2C).
 *
 *          This driver knows only the TCA9554 family and the I2C bus:
 *          - I2C device handle
 *          - 7-bit slave address (configurable, NOT board-fixed)
 *          - register map: INPUT / OUTPUT / POLARITY / CONFIG
 *          - pins 0..7
 *          - I2C transaction timeout
 *          - raw electrical output byte
 *
 *          It knows NOTHING about the board, product, or application:
 *          no Waveshare, no CallBox, no BSP channel enums, no task/tower
 *          semantics, no MQTT/WCS.
 *
 * @note    Thread safety: the driver is NOT thread-safe. It keeps instance
 *          state (bus/device handle, address, timeout, shadow helpers) but
 *          does not create a mutex. The caller owns serialization of all
 *          I2C transactions (e.g. BSP_DO serializes read-modify-write).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#ifndef TCA9554_H
#define TCA9554_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCA9554_PIN_COUNT 8

/**
 * @brief Runtime configuration for one TCA9554 device instance.
 *
 * The slave address (e.g. 0x20 on the current board) is BOARD information
 * and must be provided here — the driver never assumes a fixed address.
 */
typedef struct {
    i2c_master_bus_handle_t bus;        /* Existing I2C master bus */
    uint8_t address;                    /* 7-bit slave address, e.g. 0x20 */
    uint32_t clock_hz;                  /* Per-device SCL frequency, e.g. 400 kHz */
    uint32_t timeout_ms;                /* I2C transaction timeout in ms */
} tca9554_config_t;

/**
 * @brief TCA9554 device instance (caller-owned storage).
 *
 * Contains the device handle and configuration snapshot.  The driver is
 * non-thread-safe; callers must serialize all accesses.
 */
typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint32_t timeout_ms;
} tca9554_t;

/**
 * @brief Initialize one TCA9554 device on an existing I2C master bus.
 *
 * Adds the device at `config->address`, configures all pins as outputs
 * and writes the initial safe electrical output byte.
 *
 * @param dev      Handle to populate (caller-owned storage).
 * @param config   Bus, address, clock and timeout; must be valid.
 * @param initial_outputs  Raw electrical output byte written after
 *                         configuring directions (e.g. 0xFF for inactive
 *                         active-low outputs).
 * @return ESP_OK on success; esp_err_t otherwise (no resource leak on
 *         partial failure is guaranteed by this driver).
 */
esp_err_t tca9554_init(tca9554_t *dev, const tca9554_config_t *config,
                       uint8_t initial_outputs);

/**
 * @brief Configure every pin as output (CONFIG = 0x00).
 */
esp_err_t tca9554_set_all_outputs(tca9554_t *dev);

/**
 * @brief Set the direction of a single pin.
 * @param pin    0..7
 * @param input  true = input (CONFIG bit 1), false = output (bit 0)
 */
esp_err_t tca9554_set_pin_mode(tca9554_t *dev, uint8_t pin, bool input);

/**
 * @brief Read the raw OUTPUT register (8 bits).
 * @param out  Receives the raw electrical output byte.
 */
esp_err_t tca9554_read_outputs(tca9554_t *dev, uint8_t *out);

/**
 * @brief Write the raw OUTPUT register (all 8 bits at once).
 * @param outputs  Raw electrical output byte.
 */
esp_err_t tca9554_write_outputs(tca9554_t *dev, uint8_t outputs);

/**
 * @brief Write a single output bit (read-modify-write of OUTPUT).
 * @param pin    0..7
 * @param level  raw electrical level (0 or 1)
 */
esp_err_t tca9554_write_pin(tca9554_t *dev, uint8_t pin, bool level);

/**
 * @brief Toggle a single output bit (read-modify-write of OUTPUT).
 * @param pin    0..7
 */
esp_err_t tca9554_toggle_pin(tca9554_t *dev, uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* TCA9554_H */
