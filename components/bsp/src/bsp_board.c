/**
 * @file    bsp_board.c
 * @brief   Khởi tạo toàn bộ board Waveshare ESP32-S3 8DI/8DO từ một lệnh gọi.
 *
 *          Thứ tự khởi tạo: DI (GPIO 4..11) → bus I2C board (bsp_i2c) →
 *          DO (TCA9554PWR trên bus đó) → buzzer (GPIO46 LEDC).
 *          Nếu một thành phần lỗi, hàm trả về mã lỗi ngay.
 *
 *          Board là OWNER của bus I2C (bsp_i2c): nếu bsp_do_init() thất bại
 *          sau khi bus đã mở, board thử đóng bus để không rò rỉ tài nguyên.
 *
 * @note    Không khởi tạo Ethernet ở đây — W5500 được khởi tạo riêng bởi
 *          bsp_eth_init() sau khi app tạo esp-netif/esp-event.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_board.h — API khởi tạo board
 * @see     bsp_i2c.h — bus I2C board (private)
 * @see     bsp_do.c / bsp_di.c / bsp_buzzer.c — các driver thành phần
 */
#include "bsp_board.h"
#include "bsp_buzzer.h"
#include "bsp_di.h"
#include "bsp_do.h"
#include "bsp_i2c.h"
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

    /* BƯỚC 2 — Mở bus I2C của BOARD (chủ sở hữu bus: bsp_i2c).
     * Phải mở TRƯỚC bsp_do_init() vì DO consume bus này. */
    ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_i2c_init failed: %s", esp_err_to_name(ret));
        return ret;   /* Không có bus → không thể chạy TCA9554 */
    }

    /* BƯỚC 3 — Khởi tạo 8 đầu ra số qua TCA9554 (I2C 0x20, mặc định OFF). */
    ret = bsp_do_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_do_init failed: %s", esp_err_to_name(ret));
        /* Board sở hữu bus → thử đóng bus đã mở ở BƯỚC 2.
         * Lưu ý: nếu TCA device trong bsp_do chưa detach được, i2c_del
         * có thể fail (bus vẫn còn child) — điều này hợp lệ: log cleanup
         * error, GIỮ nguyên primary error của bsp_do_init. */
        esp_err_t cleanup_ret = bsp_i2c_deinit();
        if (cleanup_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clean up board I2C bus after DO init failure: %s",
                     esp_err_to_name(cleanup_ret));
        }
        return ret;   /* Giữ primary error */
    }

    /* BƯỚC 4 — Khởi tạo buzzer trên board (GPIO46, PWM LEDC). */
    ret = bsp_buzzer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_buzzer_init failed: %s", esp_err_to_name(ret));
        return ret;   /* Buzzer lỗi: không nghiêm trọng bằng DI/DO, vẫn dừng báo lỗi */
    }

    ESP_LOGI(TAG, "Board initialized: 8DI + 8DO (TCA9554) + buzzer");
    return ESP_OK;
}
