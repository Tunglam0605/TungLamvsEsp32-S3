#include "bsp_di.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "driver/gpio.h"

#include "bsp_pins.h"

static const char *TAG = "bsp_di";

static bool bsp_di_channel_is_valid(bsp_di_channel_t channel)
{
    return channel >= BSP_DI_1 && channel < BSP_DI_COUNT;
}

static gpio_num_t bsp_di_channel_to_gpio(bsp_di_channel_t channel)
{
    return (gpio_num_t)(BSP_DI_GPIO_FIRST + channel);
}

esp_err_t bsp_di_init(void)
{
    const uint64_t pin_mask = ((1ULL << (BSP_DI_GPIO_LAST + 1)) - 1ULL) ^
                              ((1ULL << BSP_DI_GPIO_FIRST) - 1ULL);
    const gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

esp_err_t bsp_di_read_raw(bsp_di_channel_t channel, bool *raw_high)
{
    if (raw_high == NULL || !bsp_di_channel_is_valid(channel)) {
        ESP_LOGE(TAG, "Invalid DI channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    *raw_high = gpio_get_level(bsp_di_channel_to_gpio(channel)) != 0;
    return ESP_OK;
}

esp_err_t bsp_di_read_raw_mask(uint8_t *raw_high_mask)
{
    if (raw_high_mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mask = 0;
    for (bsp_di_channel_t channel = BSP_DI_1; channel < BSP_DI_COUNT; ++channel) {
        bool raw_high;
        const esp_err_t err = bsp_di_read_raw(channel, &raw_high);
        if (err != ESP_OK) {
            return err;
        }
        if (raw_high) {
            mask |= (uint8_t)(1U << channel);
        }
    }
    *raw_high_mask = mask;
    return ESP_OK;
}

esp_err_t bsp_di_read(bsp_di_channel_t channel, bool *active)
{
    if (active == NULL || !bsp_di_channel_is_valid(channel)) {
        ESP_LOGE(TAG, "Invalid DI channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    bool raw_high;
    const esp_err_t err = bsp_di_read_raw(channel, &raw_high);
    if (err != ESP_OK) {
        return err;
    }
#if CONFIG_PLATFORM_DI_ACTIVE_LOW_PROVISIONAL
    *active = !raw_high;
#else
    *active = raw_high;
#endif
    return ESP_OK;
}

esp_err_t bsp_di_read_mask(uint8_t *active_mask)
{
    if (active_mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mask = 0;
    for (bsp_di_channel_t channel = BSP_DI_1; channel < BSP_DI_COUNT; ++channel) {
        bool active;
        const esp_err_t err = bsp_di_read(channel, &active);
        if (err != ESP_OK) {
            return err;
        }
        if (active) {
            mask |= (uint8_t)(1U << channel);
        }
    }
    *active_mask = mask;
    return ESP_OK;
}

bool bsp_di_uses_provisional_active_low(void)
{
#if CONFIG_PLATFORM_DI_ACTIVE_LOW_PROVISIONAL
    return true;
#else
    return false;
#endif
}
