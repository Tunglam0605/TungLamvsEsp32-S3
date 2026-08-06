#include "bsp_i2c_internal.h"

#include "esp_log.h"

#include "bsp_pins.h"

static const char *TAG = "bsp_i2c";
static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t config = {
        .i2c_port = BSP_I2C_PORT,
        .sda_io_num = BSP_I2C_SDA_GPIO,
        .scl_io_num = BSP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = BSP_I2C_GLITCH_IGNORE_COUNT,
        .flags.enable_internal_pullup = true,
    };
    const esp_err_t err = i2c_new_master_bus(&config, &s_i2c_bus);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2C initialized: port=%d SDA=GPIO%d SCL=GPIO%d",
                 BSP_I2C_PORT, BSP_I2C_SDA_GPIO, BSP_I2C_SCL_GPIO);
    } else {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(err));
    }
    return err;
}

i2c_master_bus_handle_t bsp_i2c_get_bus(void)
{
    return s_i2c_bus;
}
