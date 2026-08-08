/**
 * @file    bsp_i2c.h
 * @brief   Lớp bus I2C nội bộ (PRIVATE) của board — không expose cho Application.
 *
 *          BSP I2C sở hữu tài nguyên I2C của BOARD:
 *            - bộ điều khiển (controller) I2C_NUM_1
 *            - chân SCL (GPIO41) / SDA (GPIO42)
 *            - bộ lọc glitch + điện trở kéo lên nội bộ
 *            - vòng đời bus: khởi tạo (init) / xóa (deinit)
 *
 *          Các subsystem của board (bsp_do...) CHỈ consume bus đã tồn tại
 *          qua bsp_i2c_get_bus() — không tạo/xóa bus.
 *
 *          ⚠️ Đây là BSP infrastructure PRIVATE:
 *            - Header nằm trong private_include/, KHÔNG đặt trong include/
 *            - Application không được include bsp_i2c.h
 *            - Type i2c_master_bus_handle_t không xuất hiện ở public API
 *
 * @note    Tần số SCL (400 kHz) là thông tin của từng DEVICE (đặt ở
 *          tca9554_config_t.clk_hz trong bsp_do), không thuộc bus layer.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_board.c — khởi tạo bsp_i2c_init() trước bsp_do_init()
 * @see     bsp_do.c — consumer của bus (tca9554 config)
 */
#ifndef BSP_I2C_H
#define BSP_I2C_H

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mở I2C bus của board (nếu chưa có).
 *
 * - Nếu bus đã tồn tại (s_i2c_bus != NULL):
 *   trả ESP_ERR_INVALID_STATE — KHÔNG overwrite live handle.
 * - Nếu tạo mới thất bại: trả esp_err_t từ ESP-IDF, handle giữ NULL.
 *
 * @return ESP_OK nếu bus sẵn sàng.
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Lấy handle bus I2C của board.
 * @return i2c_master_bus_handle_t nếu đã init; NULL nếu chưa.
 */
i2c_master_bus_handle_t bsp_i2c_get_bus(void);

/**
 * @brief Đóng I2C bus của board.
 *
 * - s_i2c_bus == NULL → ESP_OK (idempotent, an toàn gọi nhiều lần).
 * - i2c_del_master_bus() thành công → handle = NULL, ESP_OK.
 * - i2c_del_master_bus() thất bại  → GIỮ nguyên handle, trả esp_err_t
   (không giả vờ đã đóng).
 *
 * @return ESP_OK nếu bus không còn tồn tại; esp_err_t nếu có lỗi.
 */
esp_err_t bsp_i2c_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_H */