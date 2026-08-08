/**
 * @file    bsp_di.h
 * @brief   Driver đầu vào số (8DI) của board Waveshare ESP32-S3 8DI/8DO.
 *
 *          Các đầu vào được đọc trực tiếp từ chân GPIO của ESP32-S3 (trên
 *          board có opto cách ly, active-low: chân đọc mức LOW khi đầu vào
 *          được kích hoạt/energized).
 *
 *          ═══ SƠ ĐỒ CHÂN (channel → GPIO) ═══
 *          ┌───────────┬──────────┐
 *          │ Channel   │ GPIO     │
 *          ├───────────┼──────────┤
 *          │ BSP_DI_1  │ GPIO 4   │
 *          │ BSP_DI_2  │ GPIO 5   │
 *          │ BSP_DI_3  │ GPIO 6   │
 *          │ BSP_DI_4  │ GPIO 7   │
 *          │ BSP_DI_5  │ GPIO 8   │
 *          │ BSP_DI_6  │ GPIO 9   │
 *          │ BSP_DI_7  │ GPIO 10  │
 *          │ BSP_DI_8  │ GPIO 11  │
 *          └───────────┴──────────┘
 *
 * @note    Tất cả chân được cấu hình pull-up khi khởi tạo (bsp_di_init —
 *          private, chỉ bsp_board_init gọi). Mức "active" = chân GPIO đọc
 *          LOW. Khởi tạo DI là phần của bsp_board_init() (bsp_board.h) —
 *          Application KHÔNG gọi bsp_di_init trực tiếp.
 *
 * @note    TRƯỚC KHI INIT (BSP chưa khởi tạo):
 *          - bsp_di_read(...)  → false (mọi kênh hợp lệ và cả kênh không
 *            hợp lệ) — không input nào được xem là active khi phần cứng
 *            chưa sẵn sàng.
 *          - bsp_di_read_all()  → 0x00 — cùng ý nghĩa, an toàn để gọi
 *            trước init mà không gây crash.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_types.h — định nghĩa kênh BSP_DI_x
 * @see     bsp_board.h — khởi tạo cùng toàn board
 */
#ifndef BSP_DI_H
#define BSP_DI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Đọc trạng thái một kênh đầu vào.
 * @param channel  BSP_DI_1 .. BSP_DI_8
 * @return true nếu đầu vào active (energized); false nếu không active,
 *         kênh không hợp lệ, hoặc BSP_DI chưa được khởi tạo.
 */
bool bsp_di_read(bsp_di_channel_t channel);

/**
 * @brief Đọc cả 8 đầu vào thành bitmask (bit0 = BSP_DI_1, bit7 = BSP_DI_8).
 * @return Bitmask các đầu vào đang active; 0x00 nếu BSP_DI chưa khởi tạo.
 */
uint8_t bsp_di_read_all(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DI_H */