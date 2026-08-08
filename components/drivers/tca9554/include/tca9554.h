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
 * @note    An toàn luồng: driver KHÔNG an toàn luồng. Instance chỉ giữ
 *          device handle và transaction timeout (không giữ bus handle,
 *          địa chỉ hay shadow state). Driver không tạo mutex; người gọi
 *          chịu trách nhiệm tuần tự hóa mọi giao dịch I2C (ví dụ BSP_DO
 *          tuần tự hóa read-modify-write).
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
 * Chỉ giữ những gì cần cho runtime và deinit: handle thiết bị + timeout.
 * Bus I2C do người gọi (caller) sở hữu — driver không nắm bus.
 * Driver không an toàn luồng; người gọi phải tuần tự hóa mọi truy cập.
 */
typedef struct {
    i2c_master_dev_handle_t dev;   /* Handle thiết bị trên bus (NULL nếu chưa init) */
    uint32_t timeout_ms;           /* Thời gian chờ mỗi giao dịch I2C (ms) */
} tca9554_t;

/**
 * @brief Khởi tạo một thiết bị TCA9554 trên bus I2C master đã có sẵn.
 *
 * Chỉ: kiểm tra đối số → thêm device (i2c_master_bus_add_device) → lưu
 * trạng thái instance. KHÔNG đụng thanh ghi CONFIG/OUTPUT: hướng chân và
 * trạng thái đầu ra an toàn là policy của BOARD (caller), người gọi gọi
 * tca9554_set_all_outputs()/tca9554_write_outputs() sau đó nếu cần.
 *
 * Vòng đời bắt buộc (ownership rule):
 *   INIT → USE → DEINIT → INIT lại nếu cần
 * Không cho INIT hai lần liên tiếp: nếu dev->dev != NULL (instance đang
 * active) → trả ESP_ERR_INVALID_STATE; KHÔNG tự deinit/reinit lại.
 *
 * Caller phải cung cấp instance đã zero-init trước khi gọi init lần đầu:
 *   tca9554_t dev = {0};
 * (hoặc khai báo static/global — storage tĩnh được zero-init sẵn).
 * Không có constructor framework, không cấp phát động.
 *
 * Nếu add device thất bại → dev->dev = NULL (không half-init).
 *
 * @param dev      Handle cần được điền (vùng nhớ do người gọi cấp).
 * @param config   Bus, địa chỉ, clock và timeout; phải hợp lệ.
 * @return ESP_OK nếu thành công; ESP_ERR_INVALID_ARG nếu đối số sai;
 *         ESP_ERR_INVALID_STATE nếu instance đang active (double init).
 */
esp_err_t tca9554_init(tca9554_t *dev, const tca9554_config_t *config);

/**
 * @brief Gỡ thiết bị ra khỏi bus I2C (i2c_master_bus_rm_device).
 *
 * Driver KHÔNG sở hữu bus I2C — bus do người gọi sở hữu, nên hàm này
 * không bao giờ gọi i2c_del_master_bus().
 *
 * - dev == NULL          → ESP_ERR_INVALID_ARG
 * - dev->dev == NULL     → ESP_OK (idempotent, an toàn gọi nhiều lần)
 * - rm_device SUCCESS    → dev->dev = NULL, ESP_OK
 * - rm_device FAIL       → GIỮ NGUYÊN dev->dev, trả esp_err_t từ ESP-IDF
 *                          (không giả vờ đã detach).
 *
 * @param dev  Instance đã init.
 * @return ESP_OK nếu không còn device active; esp_err_t phù hợp nếu lỗi.
 */
esp_err_t tca9554_deinit(tca9554_t *dev);

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
 * @note  Là nhiều giao dịch I2C (RMW): caller phải tuần tự hóa nếu nhiều
 *        execution context cùng truy cập thiết bị (driver không mutex).
 */
esp_err_t tca9554_write_pin(tca9554_t *dev, uint8_t pin, bool level);

/**
 * @brief Đảo một bit đầu ra đơn lẻ (read-modify-write của OUTPUT).
 * @param pin    0..7
 * @note  Là nhiều giao dịch I2C (RMW); caller phải tuần tự hóa nếu nhiều
 *        execution context cùng truy cập thiết bị (driver không mutex).
 */
esp_err_t tca9554_toggle_pin(tca9554_t *dev, uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* TCA9554_H */
