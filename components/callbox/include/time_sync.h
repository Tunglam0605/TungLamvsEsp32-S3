/**
 * @file time_sync.h
 * @brief Dịch vụ đồng hồ SNTP dùng chung cho mọi chế độ mạng của Callbox.
 */
#ifndef CALLBOX_TIME_SYNC_H
#define CALLBOX_TIME_SYNC_H

#include <stdbool.h>
#include "callbox_config.h"

/** Bắt đầu SNTP nền (background). An toàn khi gọi nhiều lần. */
void time_sync_init(const Config_t *config);

/** Chỉ true khi RTC đã chứa timestamp Unix hợp lý. */
bool time_sync_is_valid(void);

/** Thay thế máy chủ SNTP sau khi cấu hình portal hợp lệ được lưu. */
void time_sync_reconfigure(const Config_t *config);

#endif /* CALLBOX_TIME_SYNC_H */
