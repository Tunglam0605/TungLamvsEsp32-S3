/**
 * @file    io_debug.h
 * @brief   Cấp phát nhanh trạng thái I/O hiện tại cho WebUI chẩn đoán.
 *
 *          Module này là "cửa sổ quan sát" (diagnostics) của ứng dụng:
 *          nó lấy một bức ảnh tức thời (snapshot) về trạng thái đã chống
 *          nhiễu (debounced) của các đầu vào và trạng thái đang kích hoạt
 *          của các đầu ra, để trang web cấu hình (config portal) hiển thị
 *          cho kỹ thuật viên.
 *
 *          ═══ DỮ LIỆU TRẢ VỀ ═══
 *          ┌──────────────────┬────────────────────────────────────────┐
 *          │ di_active_mask   │ Bitmask 8 bit: bit set = đầu vào kích  │
 *          │                  │ hoạt (nút đang nhấn) — bit 0 = DI1     │
 *          │ do_active_mask   │ Bitmask 8 bit: bit set = đầu ra đang   │
 *          │                  │ cấp điện (ON) — bit 0 = DO1            │
 *          └──────────────────┴────────────────────────────────────────┘
 *
 *          Nguồn dữ liệu: io_handler (đầu vào) và BSP DO (đầu ra).
 *
 * @note    Module chỉ ĐỌC trạng thái, không điều khiển phần cứng — mọi
 *          thay đổi đầu ra vẫn phải qua led_control / BSP.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     io_handler.h — nguồn trạng thái đầu vào (debounced)
 * @see     bsp_do.h — nguồn trạng thái đầu ra (shadow register)
 * @see     config_portal.c — handler /api/io-status dùng module này
 */
#ifndef IO_DEBUG_H
#define IO_DEBUG_H

#include <stdint.h>

/**
 * @brief Ảnh chụp trạng thái I/O dùng cho WebUI chẩn đoán.
 *
 * @note   Cả hai mask đều theo nghĩa "tích cực" (active):
 *         - bit 1 = DI đang kích hoạt (nút nhấn xuống)
 *         - bit 1 = DO đang ON (đã được cấp điện)
 *         Không phụ thuộc vào điện áp vật lý — tầng BSP đã xử lý active-low.
 */
typedef struct {
    uint8_t di_active_mask; /* bit 0 = DI1, active = pressed/energized */
    uint8_t do_active_mask; /* bit 0 = DO1, active = output energized */
} io_debug_snapshot_t;

/**
 * @brief Đọc ảnh chụp trạng thái I/O hiện tại.
 * @param snapshot Con trỏ tới vùng nhớ người gọi cấp phát để nhận kết quả.
 *                 Nếu NULL, hàm bỏ qua (không làm gì).
 */
void io_debug_read(io_debug_snapshot_t *snapshot);

#endif /* IO_DEBUG_H */
