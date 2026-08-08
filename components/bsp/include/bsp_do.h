/**
 * @file    bsp_do.h
 * @brief   Driver đầu ra số (8DO) của board Waveshare ESP32-S3 8DI/8DO.
 *
 *          8 đầu ra được điều khiển bởi IC mở rộng I2C TCA9554PWR trên board.
 *          Các đầu ra là active-low (dạng opto/relay sink): ghi "active" =
 *          logic 0.
 *
 *          ═══ SƠ ĐỒ KÊNH (channel → chân expander) ═══
 *          ┌───────────┬──────────┐
 *          │ Channel   │ Expander │
 *          ├───────────┼──────────┤
 *          │ BSP_DO_1  │ P0       │
 *          │ BSP_DO_2  │ P1       │
 *          │ BSP_DO_3  │ P2       │
 *          │ BSP_DO_4  │ P3       │
 *          │ BSP_DO_5  │ P4       │
 *          │ BSP_DO_6  │ P5       │
 *          │ BSP_DO_7  │ P6       │
 *          │ BSP_DO_8  │ P7       │
 *          └───────────┴──────────┘
 *
 * @note    BSP lưu một "shadow register" (thanh ghi phản chiếu) trạng thái
 *          điện THÔ của thanh ghi OUTPUT — người gọi đọc lại mà không cần
 *          giao dịch I2C:
 *            - bit = 1 → chân expander điện HIGH → đầu ra TẮT (OFF)
 *            - bit = 0 → chân expander điện LOW  → đầu ra KÍCH HOẠT (ON)
 *          Đây là ẢNH ĐIỆN THÔ, KHÔNG phải bitmask "active". Muốn mask
 *          logical active (bit set = ON), dùng bsp_do_get_active_mask().
 *
 * @note    Shadow CHỈ được cập nhật sau khi giao dịch I2C ghi thanh ghi
 *          OUTPUT thành công (candidate → hardware write → commit). Nếu
 *          giao dịch thất bại, shadow giữ nguyên giá trị đã xác nhận —
 *          phần mềm không bao giờ giả vờ biết trạng thái phần cứng thật.
 *
 * @note    TRƯỚC KHI INIT (BSP_DO chưa khởi tạo):
 *          - bsp_do_write() / bsp_do_write_mask() / bsp_do_all_off()
 *            → ESP_ERR_INVALID_STATE
 *          - bsp_do_get_shadow()  → 0xFF (fallback an toàn: mọi đầu ra OFF)
 *          - bsp_do_get_active_mask() → 0x00 (không đầu ra nào được xem là
 *            active khi phần cứng chưa sẵn sàng)
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_types.h — định nghĩa kênh BSP_DO_x
 * @see     tca9554.h — driver TCA9554 cấp thấp
 * @see     bsp_board.h — khởi tạo cùng toàn board
 */
#ifndef BSP_DO_H
#define BSP_DO_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ghi một kênh đầu ra đơn lẻ.
 * @param channel  BSP_DO_1 .. BSP_DO_8
 * @param active   true = energize/ON, false = OFF
 * @return ESP_OK nếu hardware write thành công (shadow đã commit);
 *         ESP_ERR_INVALID_ARG nếu kênh không hợp lệ;
 *         ESP_ERR_INVALID_STATE nếu BSP_DO chưa khởi tạo;
 *         ESP_ERR_TIMEOUT nếu không lấy được mutex;
 *         esp_err_t từ I2C nếu giao dịch thất bại (shadow KHÔNG đổi).
 */
esp_err_t bsp_do_write(bsp_do_channel_t channel, bool active);

/**
 * @brief Ghi nhiều đầu ra cùng lúc bằng bitmask.
 * @param active_mask  bit0 = BSP_DO_1 .. bit7 = BSP_DO_8
 *                     1 = đầu ra kích hoạt (ON), 0 = tắt (OFF)
 * @return ESP_OK nếu hardware write thành công (shadow đã commit);
 *         ESP_ERR_INVALID_STATE nếu BSP_DO chưa khởi tạo;
 *         ESP_ERR_TIMEOUT nếu không lấy được mutex;
 *         esp_err_t từ I2C nếu giao dịch thất bại (shadow KHÔNG đổi).
 */
esp_err_t bsp_do_write_mask(uint8_t active_mask);

/**
 * @brief Đọc ảnh điện THÔ của thanh ghi OUTPUT (shadow register).
 * @return Ảnh điện thô: bit = 1 → chân HIGH → đầu ra OFF;
 *         bit = 0 → chân LOW → đầu ra ON.
 *         0xFF nếu BSP_DO chưa khởi tạo (fallback an toàn, mọi đầu ra OFF).
 */
uint8_t bsp_do_get_shadow(void);

/**
 * @brief Đọc bitmask đầu ra đang active (logical, đã đảo active-low).
 * @return Bitmask: bit set = đầu ra đang KÍCH HOẠT (ON).
 *         0x00 nếu BSP_DO chưa khởi tạo (không đầu ra nào được xem là
 *         active khi phần cứng chưa sẵn sàng).
 */
uint8_t bsp_do_get_active_mask(void);

/**
 * @brief Tắt tất cả đầu ra (OFF) — tương đương bsp_do_write_mask(0x00).
 * @return ESP_OK nếu thành công; cùng mã lỗi với bsp_do_write_mask.
 */
esp_err_t bsp_do_all_off(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DO_H */
