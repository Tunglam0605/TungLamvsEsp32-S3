/**
 * @file    bsp_internal.h
 * @brief   API khởi tạo nội bộ (PRIVATE) của BSP — KHÔNG expose cho Application.
 *
 *          Các hàm init theo thành phần ở đây chỉ được gọi bởi composition
 *          root bsp_board_init() (bsp_board.c) — Application chỉ gọi DUY NHẤT
 *          bsp_board_init() (public, bsp_board.h) để khởi tạo toàn board.
 *
 *          ⚠️ Đây là BSP infrastructure PRIVATE:
 *            - Header nằm trong private_include/, KHÔNG đặt trong include/
 *            - Application không được include bsp_internal.h
 *            - Ứng dụng gọi trực tiếp 3 hàm init này (không qua
 *              bsp_board_init) sẽ vi phạm thứ tự khởi tạo board (DI → I2C
 *              → DO → buzzer) và bỏ qua state initialized cấp board.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_board.c — composition root gọi các hàm này
 * @see     bsp_board.h — API public duy nhất cấp board
 */
#ifndef BSP_INTERNAL_H
#define BSP_INTERNAL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo 8 đầu vào số (GPIO 4..11, pull-up, active-low).
 * @return ESP_OK nếu GPIO đã cấu hình thành công.
 */
esp_err_t bsp_di_init(void);

/**
 * @brief Khởi tạo 8 đầu ra số qua TCA9554 (I2C 0x20).
 *
 * Yêu cầu bus I2C board đã tồn tại (bsp_i2c_init() gọi trước trong
 * bsp_board_init). Trình tự an toàn bất biến: OUTPUT = 0xFF → CONFIG = 0x00.
 * @return ESP_OK nếu expander sẵn sàng; esp_err_t phù hợp nếu lỗi.
 */
esp_err_t bsp_do_init(void);

/**
 * @brief Khởi tạo buzzer trên board (GPIO46, PWM LEDC).
 * @return ESP_OK nếu timer + channel LEDC đã cấu hình thành công.
 */
esp_err_t bsp_buzzer_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_INTERNAL_H */
