/**
 * @file    tca9554.h
 * @brief   Driver thiết bị tổng quát cho IC mở rộng I/O 8-bit TCA9554/PCA9554
 *          (GPIO qua I2C).
 *
 *          Driver chỉ biết về họ TCA9554 và bus I2C:
 *          - I2C device handle
 *          - địa chỉ slave 7-bit (cấu hình được, KHÔNG cố định theo board)
 *          - bản đồ thanh ghi: INPUT / OUTPUT / POLARITY / CONFIG
 *          - chân 0..7
 *          - thời gian chờ giao dịch I2C
 *          - byte đầu ra điện mức thô
 *
 *          Driver KHÔNG biết gì về board, sản phẩm hay ứng dụng:
 *          không Waveshare, không CallBox, không enum kênh BSP, không ngữ
 *          nghĩa task/tower, không MQTT/WCS.
 *
 * @note    An toàn luồng: driver KHÔNG an toàn luồng. Nó giữ trạng thái
 *          instance (handle bus/device, địa chỉ, timeout, bộ đếm shadow)
 *          nhưng không tạo mutex. Người gọi chịu trách nhiệm tuần tự hóa
 *          mọi giao dịch I2C (ví dụ BSP_DO tuần tự hóa read-modify-write).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#ifndef TCA9554_H
#define TCA9554_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCA9554_PIN_COUNT 8

/**
 * @brief Cấu hình runtime cho một instance TCA9554.
 *
 * Địa chỉ slave (ví dụ 0x20 trên board hiện tại) là thông tin của BOARD
 * và phải được truyền vào đây — driver không bao giờ tự giả định một
 * địa chỉ cố định.
 */
typedef struct {
    i2c_master_bus_handle_t bus;        /* Bus I2C master đã tồn tại */
    uint8_t address;                    /* Địa chỉ slave 7-bit, vd. 0x20 */
    uint32_t clock_hz;                  /* Tần số SCL riêng, vd. 400 kHz */
    uint32_t timeout_ms;                /* Thời gian chờ giao dịch I2C (ms) */
} tca9554_config_t;

/**
 * @brief Instance thiết bị TCA9554 (vùng nhớ do người gọi cấp phát).
 *
 * Chứa handle thiết bị và snapshot cấu hình. Driver không an toàn luồng;
 * người gọi phải tuần tự hóa mọi truy cập.
 */
typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint32_t timeout_ms;
} tca9554_t;

/**
 * @brief Khởi tạo một thiết bị TCA9554 trên bus I2C master đã có sẵn.
 *
 * Thêm thiết bị tại `config->address`, cấu hình tất cả chân là đầu ra,
 * và ghi byte đầu ra an toàn ban đầu.
 *
 * @param dev      Handle cần được điền (vùng nhớ do người gọi cấp).
 * @param config   Bus, địa chỉ, clock và timeout; phải hợp lệ.
 * @param initial_outputs  Byte mức điện thô được ghi sau khi cấu hình
 *                         hướng (vd. 0xFF để đầu ra active-low đều tắt).
 * @return ESP_OK nếu thành công; esp_err_t nếu khác (lỗi giữa chừng không
 *         để rò rỉ tài nguyên — do driver đảm bảo).
 */
esp_err_t tca9554_init(tca9554_t *dev, const tca9554_config_t *config,
                       uint8_t initial_outputs);

/**
 * @brief Cấu hình tất cả các chân làm đầu ra (CONFIG = 0x00).
 */
esp_err_t tca9554_set_all_outputs(tca9554_t *dev);

/**
 * @brief Đặt hướng một chân đơn lẻ.
 * @param pin    0..7
 * @param input  true = đầu vào (bit CONFIG = 1), false = đầu ra (bit = 0)
 */
esp_err_t tca9554_set_pin_mode(tca9554_t *dev, uint8_t pin, bool input);

/**
 * @brief Đọc thanh ghi OUTPUT thô (8 bit).
 * @param out  Nhận byte đầu ra điện mức thô.
 */
esp_err_t tca9554_read_outputs(tca9554_t *dev, uint8_t *out);

/**
 * @brief Ghi thanh ghi OUTPUT thô (cả 8 bit một lúc).
 * @param outputs  Byte đầu ra điện mức thô.
 */
esp_err_t tca9554_write_outputs(tca9554_t *dev, uint8_t outputs);

/**
 * @brief Ghi một bit đầu ra đơn lẻ (read-modify-write của OUTPUT).
 * @param pin    0..7
 * @param level  mức điện thô (0 hoặc 1)
 */
esp_err_t tca9554_write_pin(tca9554_t *dev, uint8_t pin, bool level);

/**
 * @brief Đảo một bit đầu ra đơn lẻ (read-modify-write của OUTPUT).
 * @param pin    0..7
 */
esp_err_t tca9554_toggle_pin(tca9554_t *dev, uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* TCA9554_H */
