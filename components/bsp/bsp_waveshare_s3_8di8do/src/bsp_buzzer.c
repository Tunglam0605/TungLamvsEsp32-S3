#include "bsp_buzzer.h"

#include "driver/ledc.h"

#include "bsp_pins.h"

#define BSP_BUZZER_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BSP_BUZZER_LEDC_TIMER LEDC_TIMER_0
#define BSP_BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BSP_BUZZER_DUTY_ON 128

static bool s_initialized;

esp_err_t bsp_buzzer_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = BSP_BUZZER_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BSP_BUZZER_LEDC_TIMER,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = BSP_BUZZER_GPIO,
        .speed_mode = BSP_BUZZER_LEDC_MODE,
        .channel = BSP_BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BSP_BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

esp_err_t bsp_buzzer_set(bool enabled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ledc_set_duty(BSP_BUZZER_LEDC_MODE,
                                  BSP_BUZZER_LEDC_CHANNEL,
                                  enabled ? BSP_BUZZER_DUTY_ON : 0);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(BSP_BUZZER_LEDC_MODE, BSP_BUZZER_LEDC_CHANNEL);
}
