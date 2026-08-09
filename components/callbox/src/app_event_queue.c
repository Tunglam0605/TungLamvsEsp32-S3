/** @file app_event_queue.c @brief Triển khai hàng đợi giới hạn cho các sự kiện ứng dụng. */
#include "app_event_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t s_queue;

esp_err_t app_event_queue_init(void)
{
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(24, sizeof(app_event_t));
    return s_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

bool app_event_send(const app_event_t *event, uint32_t timeout_ms)
{
    return s_queue && event && xQueueSend(s_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool app_event_receive(app_event_t *event, uint32_t timeout_ms)
{
    return s_queue && event && xQueueReceive(s_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
