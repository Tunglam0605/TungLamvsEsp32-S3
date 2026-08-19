/**
 * @file led_control.h
 * @brief Các lệnh gốc phần cứng không lưu trạng thái, dành riêng cho
 *        Output Renderer.
 */
#ifndef CALLBOX_LED_CONTROL_H
#define CALLBOX_LED_CONTROL_H

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_BLINK_SLOW,
    LED_BLINK_FAST,
    /* Hai nháy ngắn rồi một khoảng nghỉ dài, dành cho trạng thái WCS sync. */
    LED_BLINK_DOUBLE,
    LED_FLASH_2,
    LED_FLASH_3,
} LEDState_t;

typedef struct {
    int beep_count;
    int duration_ms;
} BuzzerCmd_t;

/* Chuẩn bị queue lệnh buzzer nghiệp vụ (chủ sở hữu: led_control). Phải gọi
 * TRƯỚC bsp_board_init() — nếu cấp phát thất bại, boot dừng ngay ở cùng
 * điểm chết như khi app_main tự tạo queue. */
esp_err_t led_control_prepare(void);

/** @return ESP_OK nếu buzzer worker đã sẵn sàng; ESP_ERR_NO_MEM khi
 *          không thể tạo task. */
esp_err_t led_control_init(void);
/**
 * Các API LED trả lỗi ghi phần cứng để Output Renderer chỉ đánh dấu trạng thái
 * đã áp dụng sau khi TCA9554 xác nhận thành công.
 */
esp_err_t set_button_led(int button_id, LEDState_t state);
esp_err_t led_control_tick(void);
esp_err_t set_tower_light(int color, LEDState_t state);
void buzzer_beep(int beep_count, int duration_ms);

#endif /* CALLBOX_LED_CONTROL_H */
