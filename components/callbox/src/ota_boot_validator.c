#include "ota_boot_validator.h"

#include <stdbool.h>
#include <stdint.h>

#include "boot_validation.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "health_monitor.h"
#include "ota_boot_validator_private.h"

#define QUALIFICATION_OBSERVATION_MS 15000U

static const char *TAG = "OTA_BOOT_VALID";
static TaskHandle_t s_qualification_task;

bool ota_boot_validator_normal_tasks_progressed(
    const uint32_t before[HEALTH_TASK_COUNT],
    const uint32_t after[HEALTH_TASK_COUNT])
{
    const health_task_id_t required[] = {
        HEALTH_TASK_IO_HANDLER, HEALTH_TASK_STATE_MACHINE,
        HEALTH_TASK_MQTT_SUPERVISOR, HEALTH_TASK_MQTT_TX,
        HEALTH_TASK_OUTPUT_RENDERER, HEALTH_TASK_NETWORK_STATUS,
        HEALTH_TASK_WIFI_SELECT,
    };
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (after[required[i]] == before[required[i]]) return false;
    }
    return true;
}

static void qualification_failure(const char *reason)
{
    ESP_LOGE(TAG, "Pending OTA image did not qualify: %s", reason);
    const esp_err_t err = boot_validation_request_rollback();
    if (err != ESP_OK) {
        /* Caller intentionally does not reboot here: the existing controlled
         * restart path keeps its safe-output and RTC-loop protections. */
        ESP_LOGE(TAG, "Rollback request failed: %s", esp_err_to_name(err));
    }
    /* ESP-IDF normally reboots inside the rollback API. If it returned (for
     * example because rollback metadata could not be written), retain the
     * established safe-output controlled restart path. A reset while still
     * pending is also handled by the bootloader on the next boot. */
    health_monitor_force_restart("ota_boot_qualification");
}

static void qualification_task(void *arg)
{
    const bool recovery = (bool)(uintptr_t)arg;
    uint32_t before[HEALTH_TASK_COUNT] = { 0 };
    uint32_t after[HEALTH_TASK_COUNT] = { 0 };
    health_monitor_get_check_in_counts(before);
    vTaskDelay(pdMS_TO_TICKS(QUALIFICATION_OBSERVATION_MS));
    health_monitor_get_check_in_counts(after);

    /* Recovery deliberately proves only its AP/WebUI + health-supervisor
     * startup path. It must not wait for STA, broker, or business workers. */
    if (!recovery && !ota_boot_validator_normal_tasks_progressed(before, after)) {
        qualification_failure("required local task heartbeat missing");
    } else {
        const esp_err_t err = boot_validation_mark_valid();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not mark qualified OTA image valid: %s",
                     esp_err_to_name(err));
            qualification_failure("mark-valid failed");
        } else {
            ESP_LOGI(TAG, "Pending OTA image qualified and marked valid (%s mode)",
                     recovery ? "recovery" : "normal");
        }
    }
    s_qualification_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_qualification(bool recovery)
{
    if (!boot_validation_is_pending()) return ESP_OK;
    if (s_qualification_task != NULL) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(qualification_task, "ota_boot_qual", 3072,
                    (void *)(uintptr_t)recovery, 9, &s_qualification_task) != pdPASS) {
        s_qualification_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_boot_validator_start_normal(void) { return start_qualification(false); }
esp_err_t ota_boot_validator_start_recovery(void) { return start_qualification(true); }

esp_err_t ota_boot_validator_handle_local_failure(const char *stage)
{
    if (!boot_validation_is_pending()) return ESP_OK;
    ESP_LOGE(TAG, "Local startup failure while pending verification: %s",
             stage ? stage : "unspecified");
    return boot_validation_request_rollback();
}
