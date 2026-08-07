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
 * @note    Tất cả chân được cấu hình pull-up khi khởi tạo (bsp_di_init).
 *          Mức "active" = chân GPIO đọc LOW.
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
 * @brief Initialize the digital-input GPIOs (pull-up enabled).
 */
esp_err_t bsp_di_init(void);

/**
 * @brief Read the state of one input channel.
 * @param channel  BSP_DI_1 .. BSP_DI_8
 * @return true if the input is active (energized), false otherwise.
 */
bool bsp_di_read(bsp_di_channel_t channel);

/**
 * @brief Read all 8 inputs as a bitmask (bit0 = BSP_DI_1, bit7 = BSP_DI_8).
 * @return Bitmask of active inputs.
 */
uint8_t bsp_di_read_all(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DI_H */