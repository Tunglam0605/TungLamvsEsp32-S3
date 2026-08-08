/**
 * @file    callbox_io.h
 * @brief   Ánh xạ I/O giữa vai trò ứng dụng Callbox và kênh board Waveshare.
 *
 *          Đây là NƠI DUY NHẤT ánh xạ vai trò nghiệp vụ (ví dụ "nút task 1")
 *          sang kênh BSP (ví dụ BSP_DI_1). Logic ứng dụng dùng tên vai trò;
 *          BSP vẫn tổng quát với board.
 *
 *          ═══ ĐẤU NỐI VẬT LÝ ═══
 *          ┌───────────────┬───────────┬───────────────────────────────┐
 *          │ Vai trò       │ Kênh      │ Mô tả                         │
 *          ├───────────────┼───────────┼───────────────────────────────┤
 *          │ btn_task1     │ DI3       │ Nút Exchange Cart             │
 *          │ btn_task2     │ DI2       │ Nút Supply Empty              │
 *          │ btn_cancel    │ DI1       │ Nút Cancel                    │
 *          │ buzzer        │ DO1       │ Loa buzzer (active-low)       │
 *          │ tower_red     │ DO2       │ Đèn tower ĐỎ                 │
 *          │ tower_yellow  │ DO3       │ Đèn tower VÀNG               │
 *          │ tower_green   │ DO4       │ Đèn tower XANH               │
 *          │ led_task1     │ DO7       │ LED nút task 1               │
 *          │ led_task2     │ DO6       │ LED nút task 2               │
 *          │ led_cancel    │ DO5       │ LED nút Cancel               │
 *          │ ap_status     │ DO8       │ LED trạng thái AP cục bộ     │
 *          └───────────────┴───────────┴───────────────────────────────┘
 *
 * @note    Các đầu vào là active-low (opto cách ly). Đầu ra cũng active-low.
 *          Tầng BSP đã đảo mức logic, nên ở đây chỉ dùng tên vai trò.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_types.h — định nghĩa kênh BSP_DI/BSP_DO
 * @see     bsp_di.h — driver đầu vào số
 * @see     bsp_do.h — driver đầu ra số
 */
#ifndef CALLBOX_IO_H
#define CALLBOX_IO_H

#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mapping of roles to board channels */
typedef struct {
    /* Inputs (buttons, active-low) */
    bsp_di_channel_t btn_task1;    /* Exchange cart      */
    bsp_di_channel_t btn_task2;    /* Supply empty cart  */
    bsp_di_channel_t btn_cancel;   /* Cancel             */

    /* Outputs (LEDs / tower / buzzer, active-low) */
    bsp_do_channel_t led_task1;
    bsp_do_channel_t led_task2;
    bsp_do_channel_t led_cancel;
    bsp_do_channel_t ap_status;

    bsp_do_channel_t tower_green;
    bsp_do_channel_t tower_yellow;
    bsp_do_channel_t tower_red;

    bsp_do_channel_t buzzer;
} callbox_io_mapping_t;

/**
 * @brief Get the default mapping for the Callbox SEWS build.
 */
const callbox_io_mapping_t *callbox_io_get_mapping(void);

#ifdef __cplusplus
}
#endif

#endif /* CALLBOX_IO_H */
