/**
 * @file    bsp_board.h
 * @brief   Điểm vào cấp board cho Waveshare ESP32-S3 POE-ETH 8DI/8DO.
 *
 *          Một lần gọi duy nhất khởi tạo toàn bộ board:
 *            - IC mở rộng I2C TCA9554PWR (điều khiển 8 đầu ra số)
 *            - 8 đầu vào số (GPIO)
 *            - Buzzer trên board (GPIO46)
 *            - Trạng thái đầu ra mặc định: tất cả OFF
 *
 *          ═══ THÀNH PHẦN ĐƯỢC KHỞI TẠO ═══
 *          ┌──────────────────┬────────────────────────────────────┐
 *          │ Thành phần       │ Chi tiết                           │
 *          ├──────────────────┼────────────────────────────────────┤
 *          │ bsp_di_init      │ 8 DI — GPIO 4..11 (pull-up)        │
 *          │ bsp_do_init      │ 8 DO — TCA9554PWR (I2C 0x20)       │
 *          │ bsp_buzzer_init  │ Buzzer GPIO46 (PWM LEDC)           │
 *          └──────────────────┴────────────────────────────────────┘
 *
 * @note    Đây là tầng BSP (Board Support Package): nó chỉ trừu tượng hóa
 *          phần cứng của board (đầu vào/đầu ra số, buzzer) cho tầng ứng dụng.
 *          BSP nhận lệnh điều khiển khái quát từ tầng ứng dụng, không hiểu
 *          ý nghĩa nghiệp vụ của từng kênh. Ethernet (W5500) được khởi tạo
 *          riêng sau khi app tạo esp-netif.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_di.h — đầu vào số
 * @see     bsp_do.h — đầu ra số
 * @see     bsp_buzzer.h — buzzer trên board
 * @see     bsp_eth.h — Ethernet W5500 (khởi tạo riêng)
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo I/O của board (I2C expander, đầu vào/ra số, buzzer).
 *        Ethernet được khởi tạo riêng sau khi app tạo esp-netif.
 * @return ESP_OK nếu thành công.
 */
esp_err_t bsp_board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */
