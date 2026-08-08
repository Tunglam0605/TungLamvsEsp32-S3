/**
 * @file    callbox_io.c
 * @brief   Bảng ánh xạ I/O thực tế (wiring) của Callbox SEWS.
 *
 *          NHỮNG con số kênh đầu ra bên dưới phản ánh đấu dây THẬT trên
 *          board: kênh DO tương ứng bit 0..7 của TCA9554, đầu vào DI là
 *          các GPIO của ESP32-S3.
 *
 *          ═══ BẢNG ĐẤU NỐI ═══
 *          ┌───────────────────────────┬───────────────────────────────┐
 *          │ ĐẦU VÀO                   │ ĐẦU RA                       │
 *          ├───────────────────────────┼───────────────────────────────┤
 *          │ DI1 GPIO4  nút cancel      │ DO1 P0  buzzer               │
 *          │ DI2 GPIO5  nút task 2      │ DO2 P1  tower đỏ             │
 *          │ DI3 GPIO6  nút task 1      │ DO3 P2  tower vàng           │
 *          │ DI4..DI8  không dùng       │ DO4 P3  tower xanh           │
 *          │                            │ DO5 P4  LED nút Cancel       │
 *          │                            │ DO6 P5  LED task 2           │
 *          │                            │ DO7 P6  LED nút task 1       │
 *          │                            │ DO8 P7  LED trạng thái AP    │
 *          └───────────────────────────┴───────────────────────────────┘
 *
 * @note    NẾU board được đấu lại dây, CHỈ sửa file này — mọi module khác
 *          (state_machine, led_control, network_status...) đều tra cứu
 *          mapping qua callbox_io_get_mapping().
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     callbox_io.h — struct mapping và API truy cập
 * @see     bsp_di.h / bsp_do.h — định nghĩa kênh
 */
#include "callbox_io.h"

/*
 * IMPORTANT: the output channel numbers below reflect the ACTUAL wiring on
 * the Waveshare board / TCA9554 expander outputs as used by the original
 * firmware (BUZZER_PIN=1, TOWER_RED=2, TOWER_YELLOW=3, TOWER_GREEN=4,
 * LED_BTN_1=5, LED_BTN_2=6, LED_BTN_3=7). DO map to TCA output bits 0..7.
 *
 *   DI1  GPIO4  button cancel      DO1 P0  buzzer
 *   DI2  GPIO5  button task 2      DO2 P1  tower red
 *   DI3  GPIO6  button task 1      DO3 P2  tower yellow
 *   DI4..DI8 unused                DO4 P3  tower green
 *                                  DO5 P4  LED cancel
 *                                  DO6 P5  LED task 2
 *                                  DO7 P6  LED task 1
 *                                  DO8 P7  AP status LED
 *
 * If the physical board is rewired, change ONLY this file.
 */
static const callbox_io_mapping_t s_mapping = {
    /* Đầu vào thực tế: Hủy=DI1, Task 2=DI2, Task 1=DI3. */
    .btn_task1     = BSP_DI_3,
    .btn_task2     = BSP_DI_2,
    .btn_cancel    = BSP_DI_1,

    /* LED đi cùng vị trí nút vật lý: Cancel=DO5, Task 2=DO6, Task 1=DO7. */
    .led_task1     = BSP_DO_7,
    .led_task2     = BSP_DO_6,
    .led_cancel    = BSP_DO_5,
    .ap_status     = BSP_DO_8,

    /* Đèn tower: đỏ (DO2), vàng (DO3), xanh (DO4) */
    .tower_red     = BSP_DO_2,
    .tower_yellow  = BSP_DO_3,
    .tower_green   = BSP_DO_4,

    /* Buzzer (DO1) */
    .buzzer        = BSP_DO_1,
};

const callbox_io_mapping_t *callbox_io_get_mapping(void)
{
    /* Trả về con trỏ tới bảng mapping tĩnh — chỉ đọc, không cần lock */
    return &s_mapping;
}
