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
/* NVS lưu cuối block đã reserve; RAM trả tuần tự từng số trong block. Mất điện
 * chỉ bỏ qua phần block chưa dùng, tuyệt đối không tái sử dụng sequence. */
static uint32_t s_reserved_high_watermark;
static bool s_initialized;

#define SEQUENCE_RESERVATION_BLOCK 64U

esp_err_t sequence_service_init(void)
{
    if (s_initialized) return ESP_OK;

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = sequence_store_load(&s_high_watermark);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot load sequence high-watermark: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    s_reserved_high_watermark = s_high_watermark;
    s_initialized = true;
    /* Sau reboot bắt đầu tại persisted+1. Phần còn lại của block trước có thể
     * bị skip; đó là chủ ý để giữ invariant không bao giờ reuse sequence. */
    ESP_LOGI(TAG, "Ready; next sequence starts after persisted reservation=%lu",
             (unsigned long)s_high_watermark);
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
        if (candidate > s_reserved_high_watermark) {
            const uint32_t remaining = UINT32_MAX - s_reserved_high_watermark;
            const uint32_t reserve_count = remaining < SEQUENCE_RESERVATION_BLOCK
                                               ? remaining
                                               : SEQUENCE_RESERVATION_BLOCK;
            if (reserve_count == 0U) {
                err = ESP_ERR_INVALID_SIZE;
            } else {
                const uint32_t reserve_to = s_reserved_high_watermark + reserve_count;
                /* Commit reservation trước khi phơi bày số đầu block. */
                err = sequence_store_save(reserve_to);
                if (err == ESP_OK) s_reserved_high_watermark = reserve_to;
            }
        }
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
