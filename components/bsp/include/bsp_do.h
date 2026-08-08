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
 *          đầu ra để người gọi có thể đọc lại mà không cần giao dịch I2C.
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
 * @brief Initialize the digital outputs via the TCA9554PWR expander.
 */
esp_err_t bsp_do_init(void);

/**
 * @brief Write a single output channel.
 * @param channel  BSP_DO_1 .. BSP_DO_8
 * @param active   true = energize/ON, false = OFF
 */
esp_err_t bsp_do_write(bsp_do_channel_t channel, bool active);

/**
 * @brief Write several outputs at once with a bitmask.
 * @param active_mask  bit0 = BSP_DO_1 .. bit7 = BSP_DO_8
 */
esp_err_t bsp_do_write_mask(uint8_t active_mask);

/**
 * @brief Read back the current output shadow state.
 * @return Bitmask: bit set = output active (energized).
 */
uint8_t bsp_do_get_shadow(void);

/**
 * @brief Read the current active output mask.
 * @return Bitmask: bit set = output active/energized.
 */
uint8_t bsp_do_get_active_mask(void);

/**
 * @brief Set all outputs inactive (OFF).
 */
esp_err_t bsp_do_all_off(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DO_H */
