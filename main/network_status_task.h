/**
 * @file    network_status_task.h
 * @brief   Task hiển thị trạng thái mạng lên phần cứng (LED AP + buzzer).
 *
 *          Module này "phản chiếu" trạng thái kết nối của thiết bị ra
 *          ngoài: bật/tắt LED trạng thái SoftAP (DO theo mapping->ap_status),
 *          phát 2 tiếng bíp khi STA vừa kết nối thành công, và tự tắt
 *          SoftAP khi STA ổn định lâu + không có client + không còn phiên
 *          cấu hình đang hoạt động.
 *
 *          ═══ VAI TRÒ ═══
 *          ┌──────────────────┬──────────────────────────────────────┐
 *          │ LED AP           │ DO ap_status: sáng khi SoftAP bật    │
 *          │ Buzzer           │ 2 bíp 2000Hz khi STA vừa kết nối     │
 *          │ Tắt AP tự động   │ STA ổn định 30s + AP không có client │
 *          └──────────────────┴──────────────────────────────────────┘
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     wifi_init.h — trạng thái STA/AP
 * @see     callbox_io.h — kênh DO đèn trạng thái AP
 * @see     config_portal.h — kiểm tra phiên cấu hình còn hoạt động
 */
#ifndef NETWORK_STATUS_TASK_H
#define NETWORK_STATUS_TASK_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Khởi tạo và chạy task hiển thị trạng thái mạng.
 * @return ESP_OK nếu task đã tạo (hoặc mới tạo). ESP_ERR_NO_MEM nếu
 *         không đủ bộ nhớ để tạo task. Gọi nhiều lần chỉ tạo 1 task.
 */
esp_err_t network_status_task_start(void);

/** Request the rescue-AP confirmation pattern on the onboard buzzer. */
void network_status_notify_rescue_ap_changed(bool enabled);

#endif /* NETWORK_STATUS_TASK_H */
