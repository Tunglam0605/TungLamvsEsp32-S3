/**
 * @file sequence_service.c
 * @brief Chủ sở hữu duy nhất của chuỗi sự kiện toàn cục Callbox.
 */
#include "sequence_service.h"

#include <limits.h>
#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sequence_store.h"

static const char *TAG = "SEQUENCE";
static SemaphoreHandle_t s_lock;
static uint32_t s_high_watermark;
static bool s_initialized;

esp_err_t sequence_service_init(void)
{
    if (s_initialized) return ESP_OK;

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = sequence_store_load(&s_high_watermark);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot load sequence high-watermark: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Ready; persisted high-watermark=%lu", (unsigned long)s_high_watermark);
    return ESP_OK;
}

esp_err_t sequence_next(uint32_t *sequence)
{
    if (sequence == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_initialized || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;

    esp_err_t err = ESP_OK;
    if (s_high_watermark == UINT32_MAX) {
        err = ESP_ERR_INVALID_SIZE;
    } else {
        const uint32_t candidate = s_high_watermark + 1U;
        /* Lưu trước khi phơi bày số: mất điện có thể bỏ qua một số, nhưng
         * không bao giờ khiến thiết bị tái sử dụng số đã cấp. */
        err = sequence_store_save(candidate);
        if (err == ESP_OK) {
            s_high_watermark = candidate;
            *sequence = candidate;
        }
    }

    xSemaphoreGive(s_lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "Allocation failed: %s", esp_err_to_name(err));
    return err;
}

uint32_t sequence_current(void)
{
    return s_high_watermark;
}
