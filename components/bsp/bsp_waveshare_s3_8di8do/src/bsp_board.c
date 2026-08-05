#include "bsp_waveshare_s3_8di8do.h"

#include "esp_log.h"

static const char *TAG = "bsp_board";

static const bsp_capabilities_t s_capabilities = {
    .has_buzzer = true,
    .has_rgb = true,
    .digital_input_count = BSP_DI_COUNT,
    .digital_output_count = BSP_DO_COUNT,
};

esp_err_t bsp_board_init(void)
{
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    // DO safe state is deliberately initialized before non-critical peripherals.
    err = bsp_do_init();
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_di_init();
    if (err != ESP_OK) {
        return err;
    }
    err = bsp_buzzer_init();
    if (err != ESP_OK) {
        return err;
    }
    err = bsp_rgb_init();
    if (err != ESP_OK) {
        return err;
    }
    err = bsp_boot_button_init();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Waveshare BSP initialized; safe DO mask=0x%02x", bsp_do_get_safe_mask());
    return ESP_OK;
}

const bsp_capabilities_t *bsp_board_get_capabilities(void)
{
    return &s_capabilities;
}
