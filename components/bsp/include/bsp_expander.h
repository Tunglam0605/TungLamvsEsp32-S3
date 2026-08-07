/**
 * @file    bsp_expander.h
 * @brief   Driver IC mở rộng I/O TCA9554PWR qua I2C (8-bit).
 *
 *          Driver cho IC mở rộng cổng I2C TCA9554/PCA9554 trên board, dùng
 *          để điều khiển 8 đầu ra số. Đây là driver cấp thấp, độc lập với
 *          board: nó chỉ cung cấp các thao tác thanh ghi I2C khái quát
 *          (đọc/ghi bit, đặt hướng chân) cho các driver cấp trên dùng.
 *
 *          ═══ BẢN ĐỒ THANH GHI TCA9554 ═══
 *          ┌────────────┬──────────┬────────────────────────────────┐
 *          │ Địa chỉ    │ Tên      │ Mô tả                          │
 *          ├────────────┼──────────┼────────────────────────────────┤
 *          │ 0x00       │ INPUT    │ Đọc trạng thái chân (đọc)      │
 *          │ 0x01       │ OUTPUT   │ Ghi trạng thái chân (ghi)      │
 *          │ 0x02       │ POLARITY │ Đảo cực (nếu cần)             │
 *          │ 0x03       │ CONFIG   │ bit=1 → input, bit=0 → output │
 *          └────────────┴──────────┴────────────────────────────────┘
 *
 *          ═══ ĐỊA CHỈ I2C ═══
 *          BSP_TCA9554_ADDR = 0x20 (A0/A1 nối GND trên board Waveshare)
 *
 * @note    Dùng driver i2c_master hiện đại của ESP-IDF (IDF v5.0+).
 *          Bus/device được sở hữu bởi bsp_board_init() — nơi gọi
 *          bsp_expander_init(). Tất cả giao dịch I2C được bảo vệ bằng mutex.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_types.h — địa chỉ kênh BSP_DO
 * @see     bsp_do.h — API đầu ra số
 */
#ifndef BSP_EXPANDER_H
#define BSP_EXPANDER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TCA9554 7-bit I2C address (A0/A1 tied low on the Waveshare board) */
#define BSP_TCA9554_ADDR          0x20

/* TCA9554 register map   */
#define BSP_TCA9554_REG_INPUT     0x00
#define BSP_TCA9554_REG_OUTPUT    0x01
#define BSP_TCA9554_REG_CONFIG    0x03  /* bit=1 → input, bit=0 → output */

typedef struct {
    i2c_master_bus_handle_t  bus;
    i2c_master_dev_handle_t  dev;
    SemaphoreHandle_t        mutex;      /* guards driver register access */
} bsp_expander_t;

/**
 * @brief Initialize the expander on an I2C master bus.
 * @param exp      Handle to populate (caller-owned storage).
 * @param bus      Existing (v6-style) I2C master bus handle.
 * @param scl_hz   Bus clock in Hz (e.g. 400000).
 * @param outputs_all_low  If true, initialize all 8 outputs to 0 (active),
 *                         otherwise 1 (inactive, default for opto DO).
 * @return ESP_OK on success.
 */
esp_err_t bsp_expander_init(bsp_expander_t *exp,
                            i2c_master_bus_handle_t bus,
                            uint32_t scl_hz,
                            bool outputs_all_low);

/**
 * @brief Configure a pin direction.
 * @param mode 0 = output, 1 = input.
 */
esp_err_t bsp_expander_set_mode(bsp_expander_t *exp, uint8_t pin, bool input);

/**
 * @brief Set every pin as output (space for future inputs).
 */
esp_err_t bsp_expander_set_all_outputs(bsp_expander_t *exp);

/**
 * @brief Read the full 8-bit output shadow register.
 */
esp_err_t bsp_expander_read_output(bsp_expander_t *exp, uint8_t *out);

/**
 * @brief Write one output bit.
 * @param pin   0..7
 * @param level 0/1
 */
esp_err_t bsp_expander_write_pin(bsp_expander_t *exp, uint8_t pin, bool level);

/**
 * @brief Write all output bits at once.
 */
esp_err_t bsp_expander_write_all(bsp_expander_t *exp, uint8_t mask);

/**
 * @brief Toggle one output bit.
 */
esp_err_t bsp_expander_toggle_pin(bsp_expander_t *exp, uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* BSP_EXPANDER_H */