#include "bsp_waveshare_s3_8di8do.h"

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

bool bsp_di_read_raw(bsp_di_channel_t channel)
{
    if (!bsp_di_channel_is_valid(channel)) {
        ESP_LOGE(TAG, "Invalid DI channel: %d", channel);
        return false;
    }
    return gpio_get_level(bsp_di_channel_to_gpio(channel)) != 0;
}

uint8_t bsp_di_read_raw_mask(void)
{
    uint8_t mask = 0;
    for (bsp_di_channel_t channel = BSP_DI_1; channel < BSP_DI_COUNT; ++channel) {
        if (bsp_di_read_raw(channel)) {
            mask |= (uint8_t)(1U << channel);
        }
    }
    return mask;
}

bool bsp_di_read(bsp_di_channel_t channel)
{
    if (!bsp_di_channel_is_valid(channel)) {
        ESP_LOGE(TAG, "Invalid DI channel: %d", channel);
        return false;
    }
    const bool raw_high = bsp_di_read_raw(channel);
#if CONFIG_PLATFORM_DI_ACTIVE_LOW_PROVISIONAL
    return !raw_high;
#else
    return raw_high;
#endif
}

uint8_t bsp_di_read_mask(void)
{
    uint8_t mask = 0;
    for (bsp_di_channel_t channel = BSP_DI_1; channel < BSP_DI_COUNT; ++channel) {
        if (bsp_di_read(channel)) {
            mask |= (uint8_t)(1U << channel);
        }
    }
    return mask;
}

bool bsp_di_uses_provisional_active_low(void)
{
#if CONFIG_PLATFORM_DI_ACTIVE_LOW_PROVISIONAL
    return true;
#else
    return false;
#endif
}
