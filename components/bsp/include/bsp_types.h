/**
 * @file    bsp_types.h
 * @brief   Kiểu dữ liệu chung cho BSP — liệt kê kênh 8 DI / 8 DO của board
 *          Waveshare ESP32-S3 8DI/8DO.
 *
 *          BSP chỉ phơi bày tài nguyên *cấp board*: 8 đầu vào số và 8 đầu ra
 *          số. Không có tên gọi ứng dụng/nghiệp vụ nào (ví dụ "nút task 1")
 *          nằm ở đây — việc ánh xạ đó thuộc về tầng ứng dụng.
 *
 *          ═══ KÊNH DI (8 opto cách ly, active-low) ═══
 *          ┌───────────┬──────────┬──────────────────────────┐
 *          │ Kênh      │ GPIO     │ Mô tả                    │
 *          ├───────────┼──────────┼──────────────────────────┤
 *          │ BSP_DI_1  │ GPIO 4   │ Đầu vào số 1 (opto)      │
 *          │ BSP_DI_2  │ GPIO 5   │ Đầu vào số 2 (opto)      │
 *          │ BSP_DI_3  │ GPIO 6   │ Đầu vào số 3 (opto)      │
 *          │ BSP_DI_4  │ GPIO 7   │ Đầu vào số 4 (opto)      │
 *          │ BSP_DI_5  │ GPIO 8   │ Đầu vào số 5 (opto)      │
 *          │ BSP_DI_6  │ GPIO 9   │ Đầu vào số 6 (opto)      │
 *          │ BSP_DI_7  │ GPIO 10  │ Đầu vào số 7 (opto)      │
 *          │ BSP_DI_8  │ GPIO 11  │ Đầu vào số 8 (opto)      │
 *          └───────────┴──────────┴──────────────────────────┘
 *
 *          ═══ KÊNH DO (8 đầu ra qua TCA9554PWR, active-low) ═══
 *          ┌───────────┬──────────┬──────────────────────────┐
 *          │ Kênh      │ Expander │ Mô tả                    │
 *          ├───────────┼──────────┼──────────────────────────┤
 *          │ BSP_DO_1  │ P0       │ Đầu ra số 1              │
 *          │ BSP_DO_2  │ P1       │ Đầu ra số 2              │
 *          │ BSP_DO_3  │ P2       │ Đầu ra số 3              │
 *          │ BSP_DO_4  │ P3       │ Đầu ra số 4              │
 *          │ BSP_DO_5  │ P4       │ Đầu ra số 5              │
 *          │ BSP_DO_6  │ P5       │ Đầu ra số 6              │
 *          │ BSP_DO_7  │ P6       │ Đầu ra số 7              │
 *          │ BSP_DO_8  │ P7       │ Đầu ra số 8              │
 *          └───────────┴──────────┴──────────────────────────┘
 *
 * @note    Active-low: mức logic 0 = kích hoạt (energized/ON).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_di.h — đọc đầu vào số
 * @see     bsp_do.h — ghi đầu ra số
 */
#ifndef BSP_TYPES_H
#define BSP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 8 opto-isolated digital inputs (DI1..DI8), active-low */
typedef enum {
    BSP_DI_1,
    BSP_DI_2,
    BSP_DI_3,
    BSP_DI_4,
    BSP_DI_5,
    BSP_DI_6,
    BSP_DI_7,
    BSP_DI_8,
    BSP_DI_COUNT
} bsp_di_channel_t;

/* 8 digital outputs behind TCA9554PWR expander (DO1..DO8), active-low */
typedef enum {
    BSP_DO_1,
    BSP_DO_2,
    BSP_DO_3,
    BSP_DO_4,
    BSP_DO_5,
    BSP_DO_6,
    BSP_DO_7,
    BSP_DO_8,
    BSP_DO_COUNT
} bsp_do_channel_t;

#ifdef __cplusplus
}
#endif

#endif /* BSP_TYPES_H */