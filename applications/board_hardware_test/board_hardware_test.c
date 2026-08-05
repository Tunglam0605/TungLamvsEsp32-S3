#include "board_hardware_test.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "sdkconfig.h"

#include "bsp_waveshare_s3_8di8do.h"

static const char *TAG = "board_hw_test";

static void board_hardware_test_log_profile(void)
{
    uint32_t flash_bytes = 0;
    const esp_err_t flash_err = esp_flash_get_physical_size(NULL, &flash_bytes);
    const size_t psram_bytes = esp_psram_get_size();

    if (flash_err == ESP_OK) {
        ESP_LOGI(TAG, "Detected Flash: %" PRIu32 " MiB", flash_bytes / (1024U * 1024U));
    } else {
        ESP_LOGW(TAG, "Unable to detect physical Flash size: %s", esp_err_to_name(flash_err));
    }
    ESP_LOGI(TAG, "Detected PSRAM: %u MiB", (unsigned)(psram_bytes / (1024U * 1024U)));

#if CONFIG_PLATFORM_BOARD_PROFILE_N16R8
    const uint32_t expected_flash_bytes = CONFIG_PLATFORM_EXPECTED_FLASH_SIZE_MB * 1024U * 1024U;
    const size_t expected_psram_bytes = CONFIG_PLATFORM_EXPECTED_PSRAM_SIZE_MB * 1024U * 1024U;
    if (flash_err == ESP_OK && flash_bytes != expected_flash_bytes) {
        ESP_LOGW(TAG, "Flash mismatch: expected %d MiB, detected %" PRIu32 " MiB",
                 CONFIG_PLATFORM_EXPECTED_FLASH_SIZE_MB,
                 flash_bytes / (1024U * 1024U));
    }
    if (psram_bytes != expected_psram_bytes) {
        ESP_LOGW(TAG, "PSRAM mismatch: expected %d MiB, detected %u MiB",
                 CONFIG_PLATFORM_EXPECTED_PSRAM_SIZE_MB,
                 (unsigned)(psram_bytes / (1024U * 1024U)));
    }
#endif
}

static void board_hardware_test_log_inputs(void)
{
    const uint8_t raw_mask = bsp_di_read_raw_mask();
    const uint8_t logical_mask = bsp_di_read_mask();
    ESP_LOGI(TAG,
             "DI raw=0x%02x logical=0x%02x (provisional raw-%s-is-active)",
             raw_mask,
             logical_mask,
             bsp_di_uses_provisional_active_low() ? "LOW" : "HIGH");
}

static void board_hardware_test_run_indicator_test(void)
{
    (void)bsp_rgb_set(32, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    (void)bsp_rgb_set(0, 32, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    (void)bsp_rgb_set(0, 0, 32);
    vTaskDelay(pdMS_TO_TICKS(150));
    (void)bsp_rgb_set(0, 0, 0);

    (void)bsp_buzzer_set(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    (void)bsp_buzzer_set(false);
}

static void board_hardware_test_run_optional_output_sequence(void)
{
#if CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE
    ESP_LOGW(TAG, "Starting enabled DO sequence; attached loads may actuate");
    for (bsp_do_channel_t channel = BSP_DO_1; channel < BSP_DO_COUNT; ++channel) {
        ESP_LOGI(TAG, "Testing DO%d", channel + 1);
        (void)bsp_do_write(channel, true);
        vTaskDelay(pdMS_TO_TICKS(250));
        (void)bsp_do_write(channel, false);
    }
#else
    ESP_LOGI(TAG, "DO sequence disabled by CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE");
#endif

    const esp_err_t err = bsp_do_apply_safe_state();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore DO safe state: %s", esp_err_to_name(err));
    }
}

static void board_hardware_test_task(void *context)
{
    (void)context;
    uint8_t previous_raw_mask = UINT8_MAX;
    uint8_t previous_logical_mask = UINT8_MAX;

    while (true) {
        const uint8_t raw_mask = bsp_di_read_raw_mask();
        const uint8_t logical_mask = bsp_di_read_mask();
        if (raw_mask != previous_raw_mask || logical_mask != previous_logical_mask) {
            board_hardware_test_log_inputs();
            previous_raw_mask = raw_mask;
            previous_logical_mask = logical_mask;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t board_hardware_test_start(void)
{
    ESP_LOGI(TAG, "Waveshare ESP32-S3-POE-ETH-8DI-8DO hardware test");
    ESP_LOGI(TAG, "Expected profile: ESP32-S3-WROOM-1U-N16R8 (provisional per connected board)");

    esp_err_t err = bsp_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BSP initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    const bsp_capabilities_t *capabilities = bsp_board_get_capabilities();
    ESP_LOGI(TAG, "Phase 0-1 BSP capabilities: DI=%u DO=%u buzzer=%d RGB=%d",
             capabilities->digital_input_count,
             capabilities->digital_output_count,
             capabilities->has_buzzer,
             capabilities->has_rgb);
    board_hardware_test_log_profile();
    board_hardware_test_log_inputs();
    ESP_LOGI(TAG, "BOOT button currently %s (provisional active-low interpretation)",
             bsp_boot_button_is_pressed() ? "pressed" : "released");
    ESP_LOGI(TAG, "DO masks: desired=0x%02x applied=0x%02x safe=0x%02x; provisional ON=%s",
             bsp_do_get_desired_mask(),
             bsp_do_get_applied_mask(),
             bsp_do_get_safe_mask(),
             bsp_do_uses_provisional_active_high() ? "register HIGH" : "register LOW");

    board_hardware_test_run_indicator_test();
    board_hardware_test_run_optional_output_sequence();

    if (xTaskCreate(board_hardware_test_task,
                    "board_hw_test",
                    4096,
                    NULL,
                    4,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
