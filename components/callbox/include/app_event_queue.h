/** @file app_event_queue.h @brief Hàng đợi (queue) trung gian cho các sự kiện ứng dụng. */
#ifndef CALLBOX_APP_EVENT_QUEUE_H
#define CALLBOX_APP_EVENT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include "app_event.h"
#include "esp_err.h"

typedef struct {
    uint32_t command_enqueued;
    uint32_t command_dropped;
    uint32_t command_purged;
    uint32_t lifecycle_coalesced;
    uint32_t resync_coalesced;
} app_event_queue_stats_t;

esp_err_t app_event_queue_init(void);
bool app_event_send(const app_event_t *event, uint32_t timeout_ms);
bool app_event_receive(app_event_t *event, uint32_t timeout_ms);

/**
 * @brief Bỏ tối đa toàn bộ dung lượng command queue theo cách hữu hạn.
 *
 * Dùng khi mở một MQTT session mới hoặc phát hiện đã mất command vì queue đầy.
 * Hàm không dùng xQueueReset và không lặp vô hạn nếu producer vẫn đang gửi.
 *
 * @return Số command đã bỏ khỏi queue trong lần gọi này.
 */
uint32_t app_event_queue_purge_commands(void);

/**
 * @brief Chụp bộ đếm chẩn đoán của hàng đợi mà không thay đổi chúng.
 *
 * Sự kiện lifecycle MQTT dùng một latch riêng sâu một phần tử nên không thể bị
 * command WCS chiếm hết chỗ. `lifecycle_coalesced` tăng khi trạng thái mới ghi
 * đè trạng thái lifecycle chưa kịp được Mission Manager đọc.
 */
void app_event_queue_get_stats(app_event_queue_stats_t *stats);

#endif /* CALLBOX_APP_EVENT_QUEUE_H */
