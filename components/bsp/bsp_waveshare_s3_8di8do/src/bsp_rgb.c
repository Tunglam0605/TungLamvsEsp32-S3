#include "bsp_rgb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#include "esp_log.h"

#include "bsp_pins.h"

#define BSP_RGB_RESOLUTION_HZ 10000000
#define BSP_RGB_RESET_TICKS 250

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static SemaphoreHandle_t s_lock;
static const char *TAG = "bsp_rgb";

static void bsp_rgb_delete_channel(void)
{
    if (s_channel == NULL) {
        return;
    }
    const esp_err_t err = rmt_del_channel(s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete RMT channel: %s", esp_err_to_name(err));
    }
    s_channel = NULL;
}

static void bsp_rgb_delete_encoder(void)
{
    if (s_encoder == NULL) {
        return;
    }
    const esp_err_t err = rmt_del_encoder(s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete RMT encoder: %s", esp_err_to_name(err));
    }
    s_encoder = NULL;
}

static rmt_symbol_word_t bsp_rgb_symbol_for_bit(bool value)
{
    return value ? (rmt_symbol_word_t){
               .level0 = 1,
               .duration0 = 9,
               .level1 = 0,
               .duration1 = 3,
           }
                 : (rmt_symbol_word_t){
               .level0 = 1,
               .duration0 = 3,
               .level1 = 0,
               .duration1 = 9,
           };
}

esp_err_t bsp_rgb_init(void)
{
    if (s_channel != NULL) {
        return ESP_OK;
    }

    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = BSP_RGB_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = BSP_RGB_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&channel_config, &s_channel);
    if (err != ESP_OK) {
        return err;
    }

    // ESP-IDF 6.1 intentionally defines this configuration as an empty struct.
    const rmt_copy_encoder_config_t encoder_config = {};
    err = rmt_new_copy_encoder(&encoder_config, &s_encoder);
    if (err != ESP_OK) {
        bsp_rgb_delete_channel();
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        bsp_rgb_delete_encoder();
        bsp_rgb_delete_channel();
        return ESP_ERR_NO_MEM;
    }

    err = rmt_enable(s_channel);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_lock);
        bsp_rgb_delete_encoder();
        bsp_rgb_delete_channel();
        s_lock = NULL;
    }
    return err;
}

esp_err_t bsp_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{
    if (s_channel == NULL || s_encoder == NULL || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const uint8_t grb[] = {green, red, blue};
    rmt_symbol_word_t symbols[25] = {0};
    size_t symbol_index = 0;
    for (size_t color_index = 0; color_index < sizeof(grb); ++color_index) {
        for (uint8_t bit = 0x80; bit != 0; bit >>= 1) {
            symbols[symbol_index++] = bsp_rgb_symbol_for_bit((grb[color_index] & bit) != 0);
        }
    }
    symbols[symbol_index] = (rmt_symbol_word_t){
        .level0 = 0,
        .duration0 = BSP_RGB_RESET_TICKS,
        .level1 = 0,
        .duration1 = BSP_RGB_RESET_TICKS,
    };

    const rmt_transmit_config_t tx_config = {.loop_count = 0};
    esp_err_t err = rmt_transmit(s_channel, s_encoder, symbols, sizeof(symbols), &tx_config);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s_channel, 100);
    }
    xSemaphoreGive(s_lock);
    return err;
}
