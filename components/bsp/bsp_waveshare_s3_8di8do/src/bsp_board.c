#include "bsp_board.h"
#include "bsp_boot_button.h"
#include "bsp_buzzer.h"
#include "bsp_di.h"
#include "bsp_do.h"
#include "bsp_rgb.h"

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
    // DO safe state is deliberately initialized before non-critical peripherals.
    esp_err_t err = bsp_do_init();
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

    bsp_do_status_t do_status;
    err = bsp_do_get_status(&do_status);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "Waveshare BSP initialized; safe DO mask=0x%02x", do_status.safe_mask);
    return ESP_OK;
}

const bsp_capabilities_t *bsp_board_get_capabilities(void)
{
    return &s_capabilities;
}
