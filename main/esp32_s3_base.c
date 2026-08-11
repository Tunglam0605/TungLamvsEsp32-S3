/** @file esp32_s3_base.c @brief Firmware nen chung, khong chua logic san pham. */
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "bsp_board.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(bsp_board_init());
    ESP_LOGI("BASE_APP", "ESP32-S3 common base ready");
}
