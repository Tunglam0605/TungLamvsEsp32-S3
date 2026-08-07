/**
 * @file    bsp_buzzer.h
 * @brief   Buzzer thụ động trên board (GPIO46) của Waveshare ESP32-S3-POE-ETH-8DI-8DO.
 *
 *          Buzzer được điều khiển bằng PWM LEDC:
 *          ┌──────────────────┬────────────────────────────────────┐
 *          │ Thông số        │ Giá trị                            │
 *          ├──────────────────┼────────────────────────────────────┤
 *          │ GPIO             │ GPIO 46                            │
 *          │ LEDC timer       │ LEDC_TIMER_1 (low speed)          │
 *          │ LEDC channel     │ LEDC_CHANNEL_1                    │
 *          │ Độ phân giải     │ 10-bit (duty 0..1023)             │
 *          │ Tần số mặc định  │ 2000 Hz                            │
 *          └──────────────────┴────────────────────────────────────┘
 *
 * @note    Đây là buzzer *thụ động*: cần PWM ở tần số mong muốn để kêu
 *          (bsp_buzzer_set). Gọi bsp_buzzer_off() để tắt âm thanh.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_board.h — khởi tạo buzzer trong bsp_board_init()
 */
#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_buzzer_init(void);
esp_err_t bsp_buzzer_set(uint32_t frequency_hz, uint8_t duty_percent);
esp_err_t bsp_buzzer_off(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUZZER_H */
