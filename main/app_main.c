#include "esp_err.h"
#include "esp_log.h"

#include "board_hardware_test.h"

static const char *TAG = "platform_boot";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting board_hardware_test firmware");
    esp_err_t err = board_hardware_test_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware test startup failed: %s", esp_err_to_name(err));
    }
}
