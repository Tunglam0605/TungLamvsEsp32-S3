#include "board_hardware_test.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "bsp_waveshare_s3_8di8do.h"

static const char *TAG = "board_hw_test";

static void board_hardware_test_log_firmware_identity(void)
{
    const esp_app_desc_t *description = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware: project=%s version=%s idf=%s",
             description->project_name,
             description->version,
             description->idf_ver);
    ESP_LOGI(TAG, "Reset reason: %d", (int)esp_reset_reason());
}

static void board_hardware_test_log_partition_table(void)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    const esp_partition_t *storage = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);

    if (factory != NULL) {
        ESP_LOGI(TAG, "Partition table: app label=%s offset=0x%lx size=0x%lx",
                 factory->label,
                 (unsigned long)factory->address,
                 (unsigned long)factory->size);
    } else {
        ESP_LOGW(TAG, "Partition table: factory app partition not found");
    }
    if (storage != NULL) {
        ESP_LOGI(TAG, "Partition table: data label=%s offset=0x%lx size=0x%lx",
                 storage->label,
                 (unsigned long)storage->address,
                 (unsigned long)storage->size);
    }
}

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

static void board_hardware_test_log_input_masks(uint8_t raw_mask, uint8_t logical_mask)
{
    ESP_LOGI(TAG,
             "DI raw=0x%02x logical=0x%02x (provisional raw-%s-is-active)",
             raw_mask,
             logical_mask,
             bsp_di_uses_provisional_active_low() ? "LOW" : "HIGH");
}

static esp_err_t board_hardware_test_read_inputs(uint8_t *raw_mask, uint8_t *logical_mask)
{
    esp_err_t err = bsp_di_read_raw_mask(raw_mask);
    if (err != ESP_OK) {
        return err;
    }
    return bsp_di_read_mask(logical_mask);
}

static esp_err_t board_hardware_test_log_inputs(void)
{
    uint8_t raw_mask;
    uint8_t logical_mask;
    const esp_err_t err = board_hardware_test_read_inputs(&raw_mask, &logical_mask);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read DI states: %s", esp_err_to_name(err));
        return err;
    }
    board_hardware_test_log_input_masks(raw_mask, logical_mask);
    return ESP_OK;
}

static esp_err_t board_hardware_test_set_rgb_and_wait(uint8_t red, uint8_t green, uint8_t blue)
{
    const esp_err_t err = bsp_rgb_set(red, green, blue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB write failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    return ESP_OK;
}

static esp_err_t board_hardware_test_run_indicator_test(void)
{
    esp_err_t err = board_hardware_test_set_rgb_and_wait(32, 0, 0);
    if (err == ESP_OK) {
        err = board_hardware_test_set_rgb_and_wait(0, 32, 0);
    }
    if (err == ESP_OK) {
        err = board_hardware_test_set_rgb_and_wait(0, 0, 32);
    }

    const esp_err_t rgb_off_err = bsp_rgb_set(0, 0, 0);
    if (rgb_off_err != ESP_OK) {
        ESP_LOGE(TAG, "RGB off write failed: %s", esp_err_to_name(rgb_off_err));
        if (err == ESP_OK) {
            err = rgb_off_err;
        }
    }
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "RGB test command completed; visual confirmation remains HIL evidence");

    err = bsp_buzzer_set(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Buzzer enable failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    err = bsp_buzzer_set(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Buzzer disable failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Buzzer test command completed; audible confirmation remains HIL evidence");
    }
    return err;
}

static esp_err_t board_hardware_test_run_optional_output_sequence(void)
{
    esp_err_t sequence_err = ESP_OK;
#if CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE
    ESP_LOGW(TAG, "Starting enabled DO sequence; attached loads may actuate");
    for (bsp_do_channel_t channel = BSP_DO_1; channel < BSP_DO_COUNT; ++channel) {
        ESP_LOGI(TAG, "Testing DO%d", channel + 1);
        sequence_err = bsp_do_write(channel, true);
        if (sequence_err != ESP_OK) {
            ESP_LOGE(TAG, "DO%d enable failed: %s", channel + 1, esp_err_to_name(sequence_err));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        sequence_err = bsp_do_write(channel, false);
        if (sequence_err != ESP_OK) {
            ESP_LOGE(TAG, "DO%d disable failed: %s", channel + 1, esp_err_to_name(sequence_err));
            break;
        }
    }
#else
    ESP_LOGI(TAG, "DO sequence disabled by CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE");
#endif

    const esp_err_t err = bsp_do_apply_safe_state();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore DO safe state: %s", esp_err_to_name(err));
        return err;
    }
    return sequence_err;
}

static void board_hardware_test_task(void *context)
{
    (void)context;
    uint8_t previous_raw_mask = UINT8_MAX;
    uint8_t previous_logical_mask = UINT8_MAX;

    while (true) {
        uint8_t raw_mask;
        uint8_t logical_mask;
        const esp_err_t err = board_hardware_test_read_inputs(&raw_mask, &logical_mask);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "DI polling failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (raw_mask != previous_raw_mask || logical_mask != previous_logical_mask) {
            board_hardware_test_log_input_masks(raw_mask, logical_mask);
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
    board_hardware_test_log_firmware_identity();
    board_hardware_test_log_partition_table();

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
    err = board_hardware_test_log_inputs();
    if (err != ESP_OK) {
        return err;
    }
    bool boot_pressed = false;
    err = bsp_boot_button_is_pressed(&boot_pressed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BOOT button read failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "BOOT button currently %s (provisional active-low interpretation)",
             boot_pressed ? "pressed" : "released");
    bsp_do_status_t do_status;
    err = bsp_do_get_status(&do_status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to obtain DO state: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DO masks: desired=0x%02x applied=0x%02x valid=%d safe=0x%02x; provisional ON=%s",
             do_status.desired_mask,
             do_status.applied_mask,
             do_status.applied_valid,
             do_status.safe_mask,
             bsp_do_uses_provisional_active_high() ? "register HIGH" : "register LOW");

    err = board_hardware_test_run_indicator_test();
    if (err != ESP_OK) {
        return err;
    }
    err = board_hardware_test_run_optional_output_sequence();
    if (err != ESP_OK) {
        return err;
    }

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
