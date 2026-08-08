/**
 * @file    bsp_i2c.c
 * @brief   Triển khai lớp bus I2C PRIVATE của board (xem bsp_i2c.h).
 *
 *          Board Waveshare ESP32-S3 8DI/8DO:
 *            - I2C controller: I2C_NUM_1
 *            - SCL = GPIO41, SDA = GPIO42
 *            - glitch_ignore_cnt = 7 (bỏ qua nhiễu SCL ngắn)
 *            - enable_internal_pullup = true (điện trở kéo lên nội bộ)
 *            - bus handle: static s_i2c_bus (một static là đủ, không
 *              framework singleton, không cấp phát động)
 *
 *          Ownership:
 *            - bsp_i2c SỞ HỮU vòng đời bus (init/deinit) và chân SDA/SCL.
 *            - Các subsystem (bsp_do/TCA9554) chỉ consume bus.
 *            - Không xóa bus khi vẫn còn child device đang attach.
 *
 * @note    Tần số SCL 400 kHz không thuộc bus layer — nó nằm ở
 *          tca9554_config_t.clk_hz (từng device) trong bsp_do.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_i2c.h — API private
 * @see     bsp_board.c — composition root gọi bsp_i2c_init()/bsp_i2c_deinit()
 */
#include "bsp_i2c.h"
#include "esp_log.h"

static const char *TAG = "BSP_I2C";

/* Cấu hình bus I2C của board — CHỈ ở tầng này. */
#define BSP_I2C_PORT        I2C_NUM_1
#define BSP_I2C_SCL_GPIO    GPIO_NUM_41
#define BSP_I2C_SDA_GPIO    GPIO_NUM_42

/* Handle bus — static; chỉ một luồng init/deinit qua bsp_board. */
static i2c_master_bus_handle_t s_i2c_bus = NULL;

esp_err_t bsp_i2c_init(void)
{
    /* Double-init: bus đã tồn tại → reject, không overwrite live handle. */
    if (s_i2c_bus != NULL) {
        ESP_LOGE(TAG, "I2C bus already initialized (double init rejected)");
        return ESP_ERR_INVALID_STATE;
    }

    /* GPIO cố định của board — nếu bus chưa được tạo thì mới tạo. */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port        = BSP_I2C_PORT,
        .sda_io_num      = BSP_I2C_SDA_GPIO,
        .scl_io_num      = BSP_I2C_SCL_GPIO,
        .clk_source      = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        s_i2c_bus = NULL;   /* Tạo thất bại → handle không hợp lệ. */
        return ret;
    }

    ESP_LOGI(TAG, "Board I2C bus ready (SCL=%d SDA=%d)", BSP_I2C_SCL_GPIO, BSP_I2C_SDA_GPIO);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_bus(void)
{
    return s_i2c_bus;
}

esp_err_t bsp_i2c_deinit(void)
{
    /* Idempotent: bus chưa tồn tại → coi như xong. */
    if (s_i2c_bus == NULL) return ESP_OK;

    esp_err_t ret = i2c_del_master_bus(s_i2c_bus);
    if (ret != ESP_OK) {
        /* Delete fail → GIỮ nguyên handle; không giả vờ đã đóng. */
        ESP_LOGE(TAG, "Failed to delete I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    s_i2c_bus = NULL;   /* Chỉ NULL khi delete thực sự thành công. */
    return ESP_OK;
}
