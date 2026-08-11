/**
 * @file gateway_app.c
 * @brief Bootstrap Gateway; protocol laser se duoc them o layer nay.
 */
#include "gateway_app.h"
#include "bsp_can.h"
#include "esp_log.h"

esp_err_t gateway_app_run(void)
{
    esp_err_t err = bsp_can_init();
    if (err == ESP_OK) {
        ESP_LOGI("GATEWAY", "Gateway hardware transport ready");
    }
    return err;
}
