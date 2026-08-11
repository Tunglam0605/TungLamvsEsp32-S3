/** @file gateway_main.c @brief Entrypoint rieng cua nhanh Gateway. */
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "gateway_app.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(gateway_app_run());
}
