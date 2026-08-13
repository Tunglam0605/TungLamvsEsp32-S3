/**
 * @file    io_handler.h
 * @brief   Task đọc nút bấm (DI1..DI3) và phân phối lệnh LED/buzzer.
 *
 *          Module này "gom kênh" I/O ở tầng ứng dụng:
 *            - ĐỌC: lấy trạng thái 3 nút bấm chính (task 1, task 2, cancel)
 *              từ BSP DI, chống nhiễu (debounce) độc lập từng nút, rồi phát
 *              io_state_t lên queue cho máy trạng thái.
 *            - GHI: nhận lệnh LED/tower/buzzer (led_command_t) từ queue và
 *              chuyển xuống led_control để ghi phần cứng qua BSP.
 *
 *          ═══ LUỒNG DỮ LIỆU ═══
 *          ┌──────────┐   ┌──────────────┐   ┌───────────────┐
 *          │ GPIO/BSP │ → │ io_handler   │ → │ state_machine │
 *          │ DI bấm   │   │ (debounce)   │   │ (queue)       │
 *          └──────────┘   └──────────────┘   └───────────────┘
 *          ┌──────────┐   ┌──────────────┐   ┌───────────────┐
 *          │ BSP DO   │ ← │ led_control  │ ← │ io_handler    │
 *          │ (đèn/còi)│   │              │   │ (LED queue)   │
 *          └──────────┘   └──────────────┘   └───────────────┘
 *
 *          Vị trí của các nút trên board (DI1..DI3) được quyết định bởi
 *          callbox_io_mapping_t — không sửa số kênh cứng ở đây.
 *
 * @note    Trạng thái "pressed" là trạng thái logic đã được BSP đảo từ
 *          active-low sang active-high. Đừng diễn giải mức GPIO ở tầng này.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     callbox_io.h — ánh xạ nút → kênh DI/DO
 * @see     led_control.h — áp dụng lệnh LED/buzzer xuống BSP
 * @see     state_machine.c — người tiêu thụ io_state_t
 */
#ifndef _IO_HANDLER_H_
#define _IO_HANDLER_H_

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "app_event.h"

/**
 * @brief Trạng thái các đầu vào logic do io_handler phát ra.
 *
 * @note   btn_1..btn_3 mô tả 3 nút chính (1 = nhấn, 0 = thả).
 *         di_4..di_8 dành cho các đầu vào số phụ (chưa dùng, để trống 0).
 */
typedef struct {
    /* Main buttons (active = pressed) */
    uint8_t btn_1;  /* Task 1: Exchange Cart (Yellow) */
    uint8_t btn_2;  /* Task 2: Supply Empty Cart (Green) */
    uint8_t btn_3;  /* Cancel */

    /* Các đầu vào số còn lại (DI4-DI8) — dự phòng */
    uint8_t di_4;
    uint8_t di_5;
    uint8_t di_6;
    uint8_t di_7;
    uint8_t di_8;
} io_state_t;

/**
 * @brief Khởi tạo hai queue (io_queue + led_queue) và in sơ đồ mapping.
 *        Phải gọi 1 lần duy nhất từ app_main trước khi tạo task.
 */
esp_err_t io_handler_init(void);

/** @brief Lấy queue gửi trạng thái I/O cho máy trạng thái (size 1 phần tử). */
QueueHandle_t io_handler_get_io_queue(void);

/** @brief Queue các cạnh PRESSED/RELEASED đã debounce cho button_gate. */
QueueHandle_t io_handler_get_button_event_queue(void);

/** @brief Lấy queue nhận lệnh LED/buzzer từ máy trạng thái (size 1 phần tử). */
/* Mặt nạ đầu vào logic đã debounce; bit 0 = DI1, active = đang nhấn. */
/**
 * @brief Trả về mask đầu vào đã chống nhiễu (bit đặt = nút đang nhấn).
 * @return Bit 0..2 ứng với btn_task1/btn_task2/btn_cancel theo mapping.
 *         Các bit khác (DI4..DI8) luôn 0 vì chưa đọc.
 */
uint8_t io_handler_get_stable_input_mask(void);

/**
 * @brief Vòng lặp task chính: đọc nút → debounce → publish io_queue,
 *        và drain led_queue → led_control.
 * @param pvParameters Không dùng (NULL).
 */
void io_handler_task(void *pvParameters);

#endif /* _IO_HANDLER_H_ */
