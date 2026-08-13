/** @file app_event_queue.c @brief Triển khai hàng đợi giới hạn cho các sự kiện ứng dụng. */
#include "app_event_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

/* Command có thể tới theo burst, còn lifecycle chỉ biểu diễn trạng thái MQTT
 * mới nhất. Tách hai đường để command flood không thể làm mất CONNECTED /
 * DISCONNECTED và khiến Mission Manager giữ nhầm COMM_READY. */
static QueueHandle_t s_command_queue;
static QueueHandle_t s_lifecycle_latch;
static QueueHandle_t s_resync_latch;
static app_event_queue_stats_t s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

#define APP_EVENT_COMMAND_QUEUE_LENGTH 24U

static bool app_event_is_lifecycle(app_event_type_t type)
{
    return type == APP_EVENT_MQTT_CONNECTED || type == APP_EVENT_MQTT_DISCONNECTED;
}

static bool app_event_is_resync(app_event_type_t type)
{
    return type == APP_EVENT_WCS_RESYNC_REQUIRED;
}

esp_err_t app_event_queue_init(void)
{
    if (s_command_queue && s_lifecycle_latch && s_resync_latch) return ESP_OK;

    s_command_queue = xQueueCreate(APP_EVENT_COMMAND_QUEUE_LENGTH,
                                   sizeof(app_event_t));
    s_lifecycle_latch = xQueueCreate(1, sizeof(app_event_t));
    s_resync_latch = xQueueCreate(1, sizeof(app_event_t));
    if (!s_command_queue || !s_lifecycle_latch || !s_resync_latch) {
        if (s_command_queue) vQueueDelete(s_command_queue);
        if (s_lifecycle_latch) vQueueDelete(s_lifecycle_latch);
        if (s_resync_latch) vQueueDelete(s_resync_latch);
        s_command_queue = NULL;
        s_lifecycle_latch = NULL;
        s_resync_latch = NULL;
        return ESP_ERR_NO_MEM;
    }
    memset(&s_stats, 0, sizeof(s_stats));
    return ESP_OK;
}

bool app_event_send(const app_event_t *event, uint32_t timeout_ms)
{
    if (!event || !s_command_queue || !s_lifecycle_latch || !s_resync_latch) {
        return false;
    }

    if (app_event_is_lifecycle(event->type)) {
        const bool replacing = uxQueueMessagesWaiting(s_lifecycle_latch) != 0;
        const bool sent = xQueueOverwrite(s_lifecycle_latch, event) == pdPASS;
        if (sent && replacing) {
            portENTER_CRITICAL(&s_stats_lock);
            s_stats.lifecycle_coalesced++;
            portEXIT_CRITICAL(&s_stats_lock);
        }
        return sent;
    }

    if (app_event_is_resync(event->type)) {
        const bool replacing = uxQueueMessagesWaiting(s_resync_latch) != 0;
        const bool sent = xQueueOverwrite(s_resync_latch, event) == pdPASS;
        if (sent && replacing) {
            portENTER_CRITICAL(&s_stats_lock);
            s_stats.resync_coalesced++;
            portEXIT_CRITICAL(&s_stats_lock);
        }
        return sent;
    }

    const bool sent = xQueueSend(s_command_queue, event,
                                 pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    portENTER_CRITICAL(&s_stats_lock);
    if (sent) s_stats.command_enqueued++;
    else s_stats.command_dropped++;
    portEXIT_CRITICAL(&s_stats_lock);
    return sent;
}

bool app_event_receive(app_event_t *event, uint32_t timeout_ms)
{
    if (!event || !s_command_queue || !s_lifecycle_latch || !s_resync_latch) {
        return false;
    }

    /* Lifecycle luôn được ưu tiên. Mission Manager giới hạn số event mỗi chu
     * kỳ và chặn command thường khi chưa READY, nên command của phiên cũ không
     * thể chạy chen trước snapshot sync authoritative. */
    if (xQueueReceive(s_lifecycle_latch, event, 0) == pdTRUE) return true;
    if (xQueueReceive(s_resync_latch, event, 0) == pdTRUE) return true;
    return xQueueReceive(s_command_queue, event,
                         pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

uint32_t app_event_queue_purge_commands(void)
{
    if (!s_command_queue) return 0U;

    app_event_t discarded;
    uint32_t purged = 0U;
    /* Giới hạn cứng bằng độ sâu queue: producer liên tục cũng không thể giữ
     * Mission Manager trong vòng purge vô hạn. Epoch vẫn chặn mọi item cũ còn
     * sót lại do cuộc đua với producer. */
    for (uint32_t i = 0; i < APP_EVENT_COMMAND_QUEUE_LENGTH; ++i) {
        if (xQueueReceive(s_command_queue, &discarded, 0) != pdTRUE) break;
        purged++;
    }
    if (purged != 0U) {
        portENTER_CRITICAL(&s_stats_lock);
        s_stats.command_purged += purged;
        portEXIT_CRITICAL(&s_stats_lock);
    }
    return purged;
}

void app_event_queue_get_stats(app_event_queue_stats_t *stats)
{
    if (!stats) return;
    portENTER_CRITICAL(&s_stats_lock);
    *stats = s_stats;
    portEXIT_CRITICAL(&s_stats_lock);
}
