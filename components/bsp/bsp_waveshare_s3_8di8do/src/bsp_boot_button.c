#include "bsp_waveshare_s3_8di8do.h"

#include "driver/gpio.h"

#include "bsp_pins.h"

static bool s_initialized;

esp_err_t bsp_boot_button_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BSP_BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&config);
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

bool bsp_boot_button_is_pressed(void)
{
    return s_initialized && gpio_get_level(BSP_BOOT_BUTTON_GPIO) == 0;
}
