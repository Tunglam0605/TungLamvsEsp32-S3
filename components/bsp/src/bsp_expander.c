/**
 * @file    bsp_expander.c
 * @brief   Triển khai driver IC mở rộng TCA9554PWR qua I2C (8-bit).
 *
 *          Quản lý thanh ghi CONFIG (bit=0→output, bit=1→input) và OUTPUT.
 *          Tất cả giao dịch I2C dùng driver i2c_master mới của ESP-IDF.
 *
 *          ═══ CÁC THANH GHI ĐƯỢC DÙNG ═══
 *          ┌────────┬──────────────────────────────────────────────┐
 *          │ 0x00   │ INPUT  — trạng thái chân                     │
 *          │ 0x01   │ OUTPUT — trạng thái chân (đầu ra)            │
 *          │ 0x03   │ CONFIG — bit=1 input, bit=0 output           │
 *          └────────┴──────────────────────────────────────────────┘
 *
 * @note    Khởi tạo: tất cả 8 chân là đầu ra (cfg=0x00), trạng thái ban đầu
 *          là 0xFF (inactive) trừ khi outputs_all_low = true.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_expander.h — API driver
 * @see     bsp_do.c — sử dụng driver này cho 8 đầu ra
 */
#include "bsp_expander.h"
#include "esp_log.h"

static const char *TAG = "BSP_EXPANDER";

/* A stalled SDA/SCL line must return an error, never block the application
 * forever.  Normal two-byte TCA9554 writes complete in a few milliseconds. */
#define BSP_EXPANDER_I2C_TIMEOUT_MS 100

esp_err_t bsp_expander_init(bsp_expander_t *exp,
                            i2c_master_bus_handle_t bus,
                            uint32_t scl_hz,
                            bool outputs_all_low)
{
    /* Kiểm tra con trỏ: exp là nơi ghi handle, bus là nguồn — đều bắt buộc. */
    if (!exp || !bus) {
        return ESP_ERR_INVALID_ARG;
    }

    /* BƯỚC 1 — Ghi nhớ bus và tạo mutex.
     * Mutex dùng để bảo vệ các giao dịch I2C (đọc-sửa-ghi) nếu nhiều task
     * cùng gọi driver. Ta tạo ngay từ lúc init để các hàm sau dùng chung. */
    exp->bus = bus;
    exp->mutex = xSemaphoreCreateMutex();
    if (!exp->mutex) {
        return ESP_ERR_NO_MEM;
    }

    /* BƯỚC 2 — Thêm device TCA9554 vào bus (địa chỉ 0x20).
     * dev_addr_length: 7-bit (chuẩn I2C).
     * scl_speed_hz: tần số I2C riêng cho thiết bị này (VD: 400 kHz). */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_TCA9554_ADDR,
        .scl_speed_hz    = scl_hz,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &exp->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TCA9554 device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* BƯỚC 3 — Đặt tất cả 8 chân là OUTPUT.
     * Thanh ghi CONFIG (0x03): bit=0 → output, bit=1 → input.
     * cfg=0x00: cả 8 chân đều output (phù hợp board 8DO). */
    uint8_t cfg = 0x00;
    ret = i2c_master_transmit(exp->dev, (uint8_t[]){BSP_TCA9554_REG_CONFIG, cfg}, 2,
                              BSP_EXPANDER_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure TCA9554 outputs: %s", esp_err_to_name(ret));
        return ret;
    }

    /* BƯỚC 4 — Trạng thái ban đầu của OUTPUT.
     *   outputs_all_low = true  → ghi 0x00 (mức logic-0 trên cả 8 chân)
     *   outputs_all_low = false → ghi 0xFF (inactive, đúng active-low). */
    uint8_t init = outputs_all_low ? 0x00 : 0xFF;
    ret = i2c_master_transmit(exp->dev, (uint8_t[]){BSP_TCA9554_REG_OUTPUT, init}, 2,
                              BSP_EXPANDER_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init TCA9554 outputs: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA9554PWR ready (addr=0x%02X, %lu Hz)", BSP_TCA9554_ADDR, (unsigned long)scl_hz);
    return ESP_OK;
}

esp_err_t bsp_expander_set_all_outputs(bsp_expander_t *exp)
{
    if (!exp) return ESP_ERR_INVALID_ARG;
    /* Cấu hình lại toàn bộ 8 chân thành output (CONFIG=0x00).
     * Giữ chức năng này cho trường hợp muốn reset lại hướng chân. */
    return i2c_master_transmit(exp->dev, (uint8_t[]){BSP_TCA9554_REG_CONFIG, 0x00}, 2,
                               BSP_EXPANDER_I2C_TIMEOUT_MS);
}

esp_err_t bsp_expander_set_mode(bsp_expander_t *exp, uint8_t pin, bool input)
{
    if (!exp || pin > 7) return ESP_ERR_INVALID_ARG;

    /* Đọc-thanh-ghi-CONFIG hiện tại trước (transmit_receive với 1 byte reg). */
    uint8_t cfg = 0;
    esp_err_t ret = i2c_master_transmit_receive(exp->dev,
                                                 (uint8_t[]){BSP_TCA9554_REG_CONFIG}, 1,
                                                 &cfg, 1, BSP_EXPANDER_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) return ret;

    /* Sửa bit đúng 1 chân: input=1 → set bit, output=1 → xóa bit. */
    if (input) cfg |= (1 << pin);
    else cfg &= ~(1 << pin);

    /* Ghi ngược thanh CONFIG với giá trị mới. */
    return i2c_master_transmit(exp->dev, (uint8_t[]){BSP_TCA9554_REG_CONFIG, cfg}, 2,
                               BSP_EXPANDER_I2C_TIMEOUT_MS);
}

esp_err_t bsp_expander_read_output(bsp_expander_t *exp, uint8_t *out)
{
    if (!exp || !out) return ESP_ERR_INVALID_ARG;

    /* Gửi đúng thanh ghi OUTPUT (0x01) và nhận 1 byte trạng thái. */
    uint8_t rx = 0;
    esp_err_t ret = i2c_master_transmit_receive(exp->dev,
                                                 (uint8_t[]){BSP_TCA9554_REG_OUTPUT}, 1,
                                                 &rx, 1, BSP_EXPANDER_I2C_TIMEOUT_MS);
    if (ret == ESP_OK) *out = rx;
    return ret;
}

esp_err_t bsp_expander_write_pin(bsp_expander_t *exp, uint8_t pin, bool level)
{
    if (!exp || pin > 7) return ESP_ERR_INVALID_ARG;

    /* Nhắc: ghi 1 pin cần đọc-thanh-ghi trước (đọc → sửa → ghi). */
    uint8_t out = 0;
    esp_err_t ret = bsp_expander_read_output(exp, &out);
    if (ret != ESP_OK) return ret;

    /* Bật/xóa bit của pin trong thanh OUTPUT. */
    if (level) out |= (1 << pin);
    else out &= ~(1 << pin);

    /* Ghi toàn bộ OUTPUT trở lại chip. */
    return i2c_master_transmit(exp->dev, (uint8_t[]){BSP_TCA9554_REG_OUTPUT, out}, 2,
                               BSP_EXPANDER_I2C_TIMEOUT_MS);
}

esp_err_t bsp_expander_write_all(bsp_expander_t *exp, uint8_t mask)
{
    if (!exp) return ESP_ERR_INVALID_ARG;
    /* Ghi nhanh toàn bộ 8 bit trong một lần (không cần đọc trước).
     * mask = trạng thái điện thực tế của chip. */
    return i2c_master_transmit(exp->dev, (uint8_t[]){BSP_TCA9554_REG_OUTPUT, mask}, 2,
                               BSP_EXPANDER_I2C_TIMEOUT_MS);
}

esp_err_t bsp_expander_toggle_pin(bsp_expander_t *exp, uint8_t pin)
{
    if (!exp || pin > 7) return ESP_ERR_INVALID_ARG;

    /* Đọc-trạng thái hiện tại, XOR 1 bit để đảo ngược chân đó. */
    uint8_t out = 0;
    esp_err_t ret = bsp_expander_read_output(exp, &out);
    if (ret != ESP_OK) return ret;

    out ^= (1 << pin);
    return i2c_master_transmit(exp->dev, (uint8_t[]){BSP_TCA9554_REG_OUTPUT, out}, 2,
                               BSP_EXPANDER_I2C_TIMEOUT_MS);
}
