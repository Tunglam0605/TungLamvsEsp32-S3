/**
 * @file platform_ota.c
 * @brief Implementation of Industrial Dual-Mode OTA Platform Layer.
 */

#include "platform_ota.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "PLATFORM_OTA";

static SemaphoreHandle_t s_ota_mutex = NULL;
static esp_ota_handle_t s_update_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static platform_ota_info_t s_ota_info = {0};
static bool s_header_validated = false;

static void delayed_restart_task(void *pvParameters)
{
    int delay_ms = (int)(intptr_t)pvParameters;
    if (delay_ms <= 0) {
        delay_ms = 1500;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    ESP_LOGI(TAG, "Rebooting system now into newly flashed firmware...");
    esp_restart();
}

static void trigger_restart(int delay_ms)
{
    xTaskCreate(delayed_restart_task, "ota_reboot", 2048, (void *)(intptr_t)delay_ms, 5, NULL);
}

esp_err_t platform_ota_init(void)
{
    if (!s_ota_mutex) {
        s_ota_mutex = xSemaphoreCreateMutex();
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *app_desc = esp_app_get_description();

    memset(&s_ota_info, 0, sizeof(s_ota_info));
    s_ota_info.state = PLATFORM_OTA_STATE_IDLE;

    if (running) {
        strncpy(s_ota_info.running_partition, running->label, sizeof(s_ota_info.running_partition) - 1);
    } else {
        strcpy(s_ota_info.running_partition, "unknown");
    }

    if (next) {
        strncpy(s_ota_info.next_partition, next->label, sizeof(s_ota_info.next_partition) - 1);
    } else {
        strcpy(s_ota_info.next_partition, "none");
    }

    if (app_desc) {
        strncpy(s_ota_info.app_version, app_desc->version, sizeof(s_ota_info.app_version) - 1);
        strncpy(s_ota_info.project_name, app_desc->project_name, sizeof(s_ota_info.project_name) - 1);
        snprintf(s_ota_info.compile_time, sizeof(s_ota_info.compile_time), "%s %s", app_desc->date, app_desc->time);
    }

    ESP_LOGI(TAG, "OTA Subsystem Ready. Running: [%s] (%s v%s, compiled %s). Next Target: [%s]",
             s_ota_info.running_partition, s_ota_info.project_name,
             s_ota_info.app_version, s_ota_info.compile_time, s_ota_info.next_partition);

    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Running firmware on [%s] is PENDING VERIFICATION (Rollback Armed)!", running->label);
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            ESP_LOGI(TAG, "Running firmware on [%s] is marked VALID.", running->label);
        }
    }

    return ESP_OK;
}

esp_err_t platform_ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Validating running image and cancelling rollback...");
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Image successfully validated. Rollback cancelled.");
            } else {
                ESP_LOGE(TAG, "Failed to cancel rollback: %s", esp_err_to_name(err));
            }
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t platform_ota_get_info(platform_ota_info_t *out_info)
{
    if (!out_info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ota_mutex) {
        xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    }
    memcpy(out_info, &s_ota_info, sizeof(platform_ota_info_t));
    if (s_ota_mutex) {
        xSemaphoreGive(s_ota_mutex);
    }
    return ESP_OK;
}

esp_err_t platform_ota_stream_begin(size_t image_size)
{
    if (!s_ota_mutex) {
        platform_ota_init();
    }
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);

    if (s_ota_info.state != PLATFORM_OTA_STATE_IDLE && s_ota_info.state != PLATFORM_OTA_STATE_ERROR) {
        xSemaphoreGive(s_ota_mutex);
        ESP_LOGE(TAG, "Cannot begin stream: OTA in progress (state=%d)", s_ota_info.state);
        return ESP_ERR_INVALID_STATE;
    }

    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) {
        xSemaphoreGive(s_ota_mutex);
        ESP_LOGE(TAG, "No OTA target partition found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Starting OTA stream to target partition [%s] (size: %lu bytes)...",
             s_update_partition->label, (unsigned long)s_update_partition->size);

    esp_err_t err = esp_ota_begin(s_update_partition, image_size, &s_update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Begin failed: %s", esp_err_to_name(err));
        s_ota_info.state = PLATFORM_OTA_STATE_ERROR;
        xSemaphoreGive(s_ota_mutex);
        return err;
    }

    s_ota_info.state = PLATFORM_OTA_STATE_STREAMING;
    s_ota_info.progress_percent = 0;
    s_ota_info.bytes_written = 0;
    s_ota_info.total_bytes = image_size;
    s_ota_info.error_msg[0] = '\0';
    s_header_validated = false;

    xSemaphoreGive(s_ota_mutex);
    return ESP_OK;
}

esp_err_t platform_ota_stream_write(const void *data, size_t length)
{
    if (!data || length == 0) {
        return ESP_OK;
    }

    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);

    if (s_ota_info.state != PLATFORM_OTA_STATE_STREAMING) {
        xSemaphoreGive(s_ota_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Safety Gate: Validate image header on first chunk
    if (!s_header_validated && length >= sizeof(esp_image_header_t)) {
        const esp_image_header_t *hdr = (const esp_image_header_t *)data;
        if (hdr->magic != ESP_IMAGE_HEADER_MAGIC) {
            ESP_LOGE(TAG, "Invalid image header magic: 0x%02x (expected 0x%02x)", hdr->magic, ESP_IMAGE_HEADER_MAGIC);
            snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Invalid binary image header");
            s_ota_info.state = PLATFORM_OTA_STATE_ERROR;
            esp_ota_abort(s_update_handle);
            s_update_handle = 0;
            xSemaphoreGive(s_ota_mutex);
            return ESP_ERR_INVALID_ARG;
        }
        s_header_validated = true;
        ESP_LOGI(TAG, "Binary header magic verified (0x%02x). Chip ID: %d", hdr->magic, hdr->chip_id);
    }

    esp_err_t err = esp_ota_write(s_update_handle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed at %zu bytes: %s", s_ota_info.bytes_written, esp_err_to_name(err));
        snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Write failed: %s", esp_err_to_name(err));
        s_ota_info.state = PLATFORM_OTA_STATE_ERROR;
        esp_ota_abort(s_update_handle);
        s_update_handle = 0;
        xSemaphoreGive(s_ota_mutex);
        return err;
    }

    s_ota_info.bytes_written += length;
    if (s_ota_info.total_bytes > 0) {
        s_ota_info.progress_percent = (int)((s_ota_info.bytes_written * 100) / s_ota_info.total_bytes);
        if (s_ota_info.progress_percent > 100) {
            s_ota_info.progress_percent = 100;
        }
    }

    xSemaphoreGive(s_ota_mutex);
    return ESP_OK;
}

esp_err_t platform_ota_stream_finish(void)
{
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);

    if (s_ota_info.state != PLATFORM_OTA_STATE_STREAMING) {
        xSemaphoreGive(s_ota_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_ota_info.state = PLATFORM_OTA_STATE_VERIFYING;
    ESP_LOGI(TAG, "Finalizing OTA stream (%zu bytes written)...", s_ota_info.bytes_written);

    esp_err_t err = esp_ota_end(s_update_handle);
    s_update_handle = 0;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed (checksum / image validation error): %s", esp_err_to_name(err));
        snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Validation failed: %s", esp_err_to_name(err));
        s_ota_info.state = PLATFORM_OTA_STATE_ERROR;
        xSemaphoreGive(s_ota_mutex);
        return err;
    }

    err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Set boot failed: %s", esp_err_to_name(err));
        s_ota_info.state = PLATFORM_OTA_STATE_ERROR;
        xSemaphoreGive(s_ota_mutex);
        return err;
    }

    s_ota_info.state = PLATFORM_OTA_STATE_SUCCESS_REBOOTING;
    s_ota_info.progress_percent = 100;
    ESP_LOGI(TAG, "OTA Successfully Written to [%s]! Triggering system reboot in 2s...", s_update_partition->label);

    xSemaphoreGive(s_ota_mutex);

    trigger_restart(2000);
    return ESP_OK;
}

void platform_ota_stream_abort(void)
{
    if (!s_ota_mutex) return;
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    if (s_update_handle) {
        esp_ota_abort(s_update_handle);
        s_update_handle = 0;
    }
    s_ota_info.state = PLATFORM_OTA_STATE_IDLE;
    s_ota_info.bytes_written = 0;
    s_ota_info.progress_percent = 0;
    xSemaphoreGive(s_ota_mutex);
}

typedef struct {
    char url[256];
    platform_ota_progress_cb_t progress_cb;
    void *user_ctx;
} ota_url_task_params_t;

static void ota_url_worker_task(void *pvParameters)
{
    ota_url_task_params_t *params = (ota_url_task_params_t *)pvParameters;
    ESP_LOGI(TAG, "Starting URL OTA worker for: %s", params->url);

    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    s_ota_info.state = PLATFORM_OTA_STATE_DOWNLOADING;
    s_ota_info.progress_percent = 0;
    s_ota_info.bytes_written = 0;
    s_ota_info.error_msg[0] = '\0';
    xSemaphoreGive(s_ota_mutex);

    esp_http_client_config_t http_config = {
        .url = params->url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
        s_ota_info.state = PLATFORM_OTA_STATE_ERROR;
        snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "HTTP connect failed");
        xSemaphoreGive(s_ota_mutex);
        if (params->progress_cb) {
            params->progress_cb(-1, 0, 0, params->user_ctx);
        }
        free(params);
        vTaskDelete(NULL);
        return;
    }

    int total_bytes = esp_https_ota_get_image_size(https_ota_handle);
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    s_ota_info.total_bytes = total_bytes > 0 ? total_bytes : 0;
    xSemaphoreGive(s_ota_mutex);

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
        int percent = total_bytes > 0 ? (int)((read_len * 100) / total_bytes) : 0;

        xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
        s_ota_info.bytes_written = read_len;
        s_ota_info.progress_percent = percent;
        xSemaphoreGive(s_ota_mutex);

        if (params->progress_cb) {
            params->progress_cb(percent, read_len, total_bytes, params->user_ctx);
        }
    }

    if (err == ESP_OK) {
        err = esp_https_ota_finish(https_ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTPS OTA upgrade successful! Rebooting...");
            xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
            s_ota_info.state = PLATFORM_OTA_STATE_SUCCESS_REBOOTING;
            s_ota_info.progress_percent = 100;
            xSemaphoreGive(s_ota_mutex);
            if (params->progress_cb) {
                params->progress_cb(100, s_ota_info.bytes_written, s_ota_info.total_bytes, params->user_ctx);
            }
            free(params);
            trigger_restart(2000);
            vTaskDelete(NULL);
            return;
        }
    }

    ESP_LOGE(TAG, "HTTPS OTA failed: %s", esp_err_to_name(err));
    esp_https_ota_abort(https_ota_handle);
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    s_ota_info.state = PLATFORM_OTA_STATE_ERROR;
    snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Download failed: %s", esp_err_to_name(err));
    xSemaphoreGive(s_ota_mutex);
    if (params->progress_cb) {
        params->progress_cb(-1, 0, 0, params->user_ctx);
    }
    free(params);
    vTaskDelete(NULL);
}

esp_err_t platform_ota_start_from_url(const char *url, platform_ota_progress_cb_t progress_cb, void *user_ctx)
{
    if (!url || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ota_mutex) {
        platform_ota_init();
    }

    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    if (s_ota_info.state != PLATFORM_OTA_STATE_IDLE && s_ota_info.state != PLATFORM_OTA_STATE_ERROR) {
        xSemaphoreGive(s_ota_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_ota_mutex);

    ota_url_task_params_t *params = (ota_url_task_params_t *)malloc(sizeof(ota_url_task_params_t));
    if (!params) {
        return ESP_ERR_NO_MEM;
    }
    strncpy(params->url, url, sizeof(params->url) - 1);
    params->url[sizeof(params->url) - 1] = '\0';
    params->progress_cb = progress_cb;
    params->user_ctx = user_ctx;

    BaseType_t ret = xTaskCreate(ota_url_worker_task, "ota_url_worker", 8192, params, 5, NULL);
    if (ret != pdPASS) {
        free(params);
        return ESP_FAIL;
    }

    return ESP_OK;
}
