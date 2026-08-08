/**
 * @file    tca9554.c
 * @brief   Triển khai driver IC mở rộng I/O 8-bit TCA9554/PCA9554 qua I2C.
 *
 *          Cài đặt các thao tác thanh ghi đã khai báo ở tca9554.h.
 *          Bản đồ thanh ghi là chi tiết triển khai và được giữ kín trong
 *          tệp này.
 *
 * @note    An toàn luồng: driver KHÔNG an toàn luồng. Nó giữ trạng thái
 *          instance (handle bus/device, địa chỉ, timeout) nhưng không tạo
 *          mutex. Người gọi chịu trách nhiệm tuần tự hóa mọi giao dịch I2C
 *          (ví dụ BSP_DO tuần tự hóa read-modify-write thanh ghi OUTPUT).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     tca9554.h — API driver công khai (public)
 */
#include "tca9554.h"

#include "esp_log.h"

static const char *TAG = "TCA9554";

/* Bản đồ thanh ghi — chi tiết triển khai, không công bố ra ngoài.
 * Bit CONFIG = 1 → đầu vào, bit = 0 → đầu ra. */
#define TCA9554_REG_INPUT    0x00
#define TCA9554_REG_OUTPUT   0x01
#define TCA9554_REG_POLARITY 0x02
#define TCA9554_REG_CONFIG   0x03

#define TCA9554_ADDR_7BIT_MAX 0x7FU   /* Địa chỉ slave 7-bit hợp lệ tối đa */

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

esp_err_t tca9554_init(tca9554_t *dev, const tca9554_config_t *config)
{
    /* Kiểm tra đối số — không silent fallback khi caller truyền sai. */
    if (!dev || !config || !config->bus || config->address == 0 ||
        config->address > TCA9554_ADDR_7BIT_MAX /* ngoài dải 7-bit */ ||
        config->clock_hz == 0 || config->timeout_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->timeout_ms = config->timeout_ms;
    dev->dev = NULL;

    i2c_device_config_t device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = config->address,
        .scl_speed_hz    = config->clock_hz,
    };
    esp_err_t ret = i2c_master_bus_add_device(config->bus, &device_cfg, &dev->dev);
    if (ret != ESP_OK) {
        /* Add thất bại → không để half-initialized object. */
        dev->dev = NULL;
        ESP_LOGE(TAG, "Failed to add TCA9554 device at 0x%02X: %s",
                 config->address, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA9554 device added (addr=0x%02X, %lu Hz)", config->address,
             (unsigned long)config->clock_hz);
    return ESP_OK;
}

esp_err_t tca9554_deinit(tca9554_t *dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (dev->dev != NULL) {
        i2c_master_bus_rm_device(dev->dev);
        dev->dev = NULL;
    }
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
