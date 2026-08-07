/**
 * @file    bsp_board.c
 * @brief   Khởi tạo toàn bộ board Waveshare ESP32-S3 8DI/8DO từ một lệnh gọi.
 *
 *          Thứ tự khởi tạo: DI (GPIO 4..11) → DO (TCA9554PWR I2C) → buzzer
 *          (GPIO46 LEDC). Nếu một thành phần lỗi, hàm trả về mã lỗi ngay.
 *
 * @note    Không khởi tạo Ethernet ở đây — W5500 được khởi tạo riêng bởi
 *          bsp_eth_init() sau khi app tạo esp-netif/esp-event.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_board.h — API khởi tạo board
 * @see     bsp_do.c / bsp_di.c / bsp_buzzer.c — các driver thành phần
 */
#include "bsp_board.h"
#include "bsp_buzzer.h"
#include "bsp_di.h"
#include "bsp_do.h"
#include "esp_log.h"

static const char *TAG = "BSP_BOARD";

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "=== Waveshare ESP32-S3 8DI/8DO board init ===");

    /* BƯỚC 1 — Khởi tạo 8 đầu vào số (GPIO 4..11, pull-up, active-low). */
    esp_err_t ret = bsp_di_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_di_init failed: %s", esp_err_to_name(ret));
        return ret;   /* Đầu vào lỗi → dừng, không khởi tạo tiếp */
    }

    /* BƯỚC 2 — Khởi tạo 8 đầu ra số qua TCA9554 (I2C 0x20, mặc định OFF). */
    ret = bsp_do_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_do_init failed: %s", esp_err_to_name(ret));
        return ret;   /* DO lỗi thì không điều khiển được đèn/buzzer */
    }

    /* BƯỚC 3 — Khởi tạo buzzer trên board (GPIO46, PWM LEDC). */
    ret = bsp_buzzer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_buzzer_init failed: %s", esp_err_to_name(ret));
        return ret;   /* Buzzer lỗi: không nghiêm trọng bằng DI/DO, vẫn dừng báo lỗi */
    }

    ESP_LOGI(TAG, "Board initialized: 8DI + 8DO (TCA9554) + buzzer");
    return ESP_OK;
}
