/**
 * @file    bsp_buzzer.c
 * @brief   Triển khai driver buzzer thụ động trên board (GPIO46).
 *
 *          Buzzer được điều khiển bằng PWM LEDC (low-speed mode).
 *
 *          ═══ CẤU HÌNH LEDC ═══
 *          ┌──────────────────┬──────────────────────────┐
 *          │ GPIO             │ GPIO 46                  │
 *          │ LEDC timer       │ TIMER_1 (low speed)      │
 *          │ LEDC channel     │ CHANNEL_1                │
 *          │ Độ phân giải     │ 10-bit (duty 0..1023)    │
 *          │ Tần số mặc định  │ 2000 Hz                  │
 *          └──────────────────┴──────────────────────────┘
 *
 * @note    Đây là buzzer thụ động: cần PWM với tần số mong muốn để kêu
 *          (bsp_buzzer_set). BSP chỉ cung cấp API tần số/độ mạnh âm thanh;
 *          tầng ứng dụng quyết định khi nào kêu và kêu bao lâu.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_buzzer.h — API buzzer
 * @see     bsp_board.h — khởi tạo buzzer trong bsp_board_init()
 * @see     bsp_internal.h — bsp_buzzer_init là init private (chỉ board gọi)
 */
#include "bsp_buzzer.h"
#include "bsp_internal.h"

#include "driver/gpio.h"   /* GPIO_NUM_46 (macro chân buzzer) */
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "BSP_BUZZER";

#define BSP_BUZZER_GPIO       GPIO_NUM_46
#define BSP_BUZZER_LEDC_TIMER LEDC_TIMER_1
#define BSP_BUZZER_LEDC_CH    LEDC_CHANNEL_1
#define BSP_BUZZER_RESOLUTION LEDC_TIMER_10_BIT
#define BSP_BUZZER_DUTY_MAX   ((1U << BSP_BUZZER_RESOLUTION) - 1U)
#define BSP_BUZZER_DEFAULT_HZ 2000U

static bool s_initialized;

esp_err_t bsp_buzzer_init(void)
{
    /* Bảo vệ: chỉ khởi tạo 1 lần. Nếu đã init rồi thì trả về ngay để tránh
     * cấu hình lại timer/channel đang chạy (gây gián đoạn âm thanh). */
    if (s_initialized) {
        return ESP_OK;
    }

    /*
     * BƯỚC 1 — Cấu hình TIMER LEDC.
     * Timer là "nguồn tạo xung PWM": nó quyết định tần số cơ bản của sóng.
     *   - speed_mode: LOW_SPEED (timer không cần clock chuyên dụng high-speed)
     *   - duty_resolution: 10-bit => duty tối đa 2^10 - 1 = 1023
     *   - freq_hz: tần số sóng vuông ban đầu 2000 Hz (có thể đổi sau)
     *   - clk_cfg: AUTO_CLK để driver tự chọn nguồn clock phù hợp
     */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BSP_BUZZER_RESOLUTION,
        .timer_num = BSP_BUZZER_LEDC_TIMER,
        .freq_hz = BSP_BUZZER_DEFAULT_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        return ret;   /* Nếu timer lỗi, không tiếp tục cấu hình channel */
    }

    /*
     * BƯỚC 2 — Cấu hình CHANNEL LEDC nối tới buzzer.
     * Channel ánh xạ timer vừa tạo vào một GPIO cụ thể (GPIO 46).
     *   - duty = 0: ban đầu tắt hoàn toàn (không kêu)
     *   - hpoint = 0: điểm bắt đầu xung ở chu kỳ 0 (mặc định chuẩn)
     */
    ledc_channel_config_t channel_cfg = {
        .gpio_num = BSP_BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BSP_BUZZER_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,   /* PWM phát liên tục, không cần ngắt */
        .timer_sel = BSP_BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel_cfg);
    if (ret == ESP_OK) {
        s_initialized = true;   /* Đánh dấu đã khởi tạo để init lần sau là no-op */
        ESP_LOGI(TAG, "On-board buzzer ready (GPIO%d)", BSP_BUZZER_GPIO);
    }
    return ret;
}

esp_err_t bsp_buzzer_set(uint32_t frequency_hz, uint8_t duty_percent)
{
    /* Phân biệt lỗi trạng thái và lỗi đối số (Phase D):
     *   - chưa init               → ESP_ERR_INVALID_STATE (trạng thái)
     *   - frequency == 0 / duty>100 → ESP_ERR_INVALID_ARG (đối số) */
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frequency_hz == 0 || duty_percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }

    /* BƯỚC 1 — Đặt TẦN SỐ: ví dụ 1000 Hz để tạo âm thanh giọng cao/thấp hơn.
     * ledc_set_freq dùng lại timer đã cấu hình ở init(). */
    esp_err_t ret = ledc_set_freq(LEDC_LOW_SPEED_MODE, BSP_BUZZER_LEDC_TIMER, frequency_hz);
    if (ret != ESP_OK) {
        return ret;
    }

    /* BƯỚC 2 — Đặt DUTY (cường độ âm thanh).
     * Chuyển %  -> giá trị 10-bit:
     *   duty = 1023 * percent / 100   (vd. 50% => 511, 100% => 1023)
     * Rồi ghi duty vào channel và bảo driver áp dụng ngay (update). */
    uint32_t duty = (BSP_BUZZER_DUTY_MAX * duty_percent) / 100U;
    ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, BSP_BUZZER_LEDC_CH, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, BSP_BUZZER_LEDC_CH);
    }
    return ret;
}

esp_err_t bsp_buzzer_off(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;   /* Chưa init mà muốn tắt => lỗi trạng thái */
    }
    /* Đặt duty = 0: hết xung PWM, buzzer không còn kêu.
     * (vẫn giữ timer/channel đã cấu hình để tái sử dụng nhanh) */
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, BSP_BUZZER_LEDC_CH, 0);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, BSP_BUZZER_LEDC_CH);
    }
    return ret;
}
