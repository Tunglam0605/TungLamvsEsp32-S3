/**
 * @file    io_debug.c
 * @brief   Triển khai module chẩn đoán I/O cho WebUI.
 *
 *          Hàm io_debug_read() gom hai nguồn dữ liệu độc lập thành một
 *          snapshot duy nhất:
 *            - Trạng thái đầu vào đã chống nhiễu (debounced) lấy từ
 *              io_handler — cùng trạng thái mà máy trạng thái đang dùng.
 *            - Trạng thái đầu ra đang kích hoạt lấy từ BSP DO shadow
 *              register (không cần giao dịch I2C).
 *
 * @note    Module này chỉ hội tụ dữ liệu về điểm đọc duy nhất cho portal;
 *          nó không thay đổi bất kỳ trạng thái phần cứng nào.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     io_debug.h — API snapshot
 * @see     io_handler.c — trạng thái đầu vào debounced
 * @see     bsp_do.c — shadow register đầu ra
 */
#include "io_debug.h"

#include "bsp_do.h"
#include "io_handler.h"

void io_debug_read(io_debug_snapshot_t *snapshot)
{
    /* Bảo vệ: nếu người gọi truyền con trỏ rỗng thì không ghi — tránh
     * ghi nhớ vùng nhớ không hợp lệ (segfault). */
    if (snapshot == NULL) return;

    /* Đầu vào: lấy mask logic đã được debounce — trùng với giá trị máy
     * trạng thái đang dùng để quyết định chuyển trạng thái. Ngoài ra,
     * mask này đã được "dịch" sang kênh cứng qua ánh xạ callbox_io. */
    snapshot->di_active_mask = io_handler_get_stable_input_mask();

    /* Đầu ra: lấy shadow register của BSP — bit set = đầu ra đang ON.
     * Không cần đọc I2C nên phản hồi rất nhanh cho WebUI. */
    snapshot->do_active_mask = bsp_do_get_active_mask();
}