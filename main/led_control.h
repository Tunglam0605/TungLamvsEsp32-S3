/**
 * @file led_control.h
 * @brief Các lệnh gốc phần cứng không lưu trạng thái, dành riêng cho
 *        Output Renderer.
 */
#ifndef CALLBOX_LED_CONTROL_H
#define CALLBOX_LED_CONTROL_H

#include <stdint.h>
#include "queues.h"

typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_BLINK_SLOW,
    LED_BLINK_FAST,
    LED_FLASH_2,
    LED_FLASH_3,
} LEDState_t;

typedef struct {
    int beep_count;
    int duration_ms;
} BuzzerCmd_t;

void led_control_init(void);
void set_button_led(int button_id, LEDState_t state);
void led_control_tick(void);
void set_tower_light(int color, LEDState_t state);
void buzzer_beep(int beep_count, int duration_ms);

#endif /* CALLBOX_LED_CONTROL_H */
