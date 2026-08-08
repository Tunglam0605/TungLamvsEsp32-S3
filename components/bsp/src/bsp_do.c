/**
 * @file    bsp_do.c
 * @brief   Triển khai driver đầu ra số (8DO) cho board Waveshare 8DI/8DO.
 *
 *          8 đầu ra được điều khiển qua IC mở rộng TCA9554PWR trên I2C.
 *          Trạng thái thực tế được lưu trong shadow register (s_out_shadow)
 *          để đọc lại mà không cần giao dịch I2C.
 *
 *          ═══ I2C CỦA EXPANDER ═══
 *          ┌──────────────┬──────────────┐
 *          │ SCL          │ GPIO 41      │
 *          │ SDA          │ GPIO 42      │
 *          │ I2C port     │ I2C_NUM_1    │
 *          │ Tần số       │ 400 kHz      │
 *          └──────────────┴──────────────┘
 *
 * @note    Đầu ra active-low: shadow bit = 1 → điện HIGH → đầu ra TẮT (OFF);
 *          shadow bit = 0 → điện LOW → đầu ra KÍCH HOẠT (ON).
 *          Shadow khởi tạo 0xFF = tất cả OFF.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_do.h — API đầu ra số
 * @see     tca9554.h — driver TCA9554 cấp thấp
 */
#include "bsp_do.h"
#include "bsp_board.h"
#include "tca9554.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "BSP_DO";

/*
 * Biến toàn cục: instance expander + shadow register của đầu ra.
 *
 * Gọn chân I2C cho TCA9554PWR trên board Waveshare:
 *   SCL = GPIO41, SDA = GPIO42, I2C_NUM_1, 400 kHz.
 * Đầu ra active-low: shadow bit 1 → điện HIGH → TẮT; bit 0 → điện LOW → ON.
 */
#define BSP_I2C_PORT        I2C_NUM_1
#define BSP_I2C_SCL_GPIO    GPIO_NUM_41
#define BSP_I2C_SDA_GPIO    GPIO_NUM_42
#define BSP_I2C_FREQ_HZ     400000
/* Địa chỉ 7-bit của TCA9554 trên board này (A0/A1 nối GND). */
#define BSP_TCA9554_ADDR    0x20
/* Thời gian chờ tối đa cho giao dịch I2C với expander (ms). */
#define BSP_DO_EXPANDER_TIMEOUT_MS 100U
#define BSP_DO_MUTEX_TIMEOUT_MS 50U

static tca9554_t s_expander;                 /* Instance TCA: chỉ handle device + timeout */
static i2c_master_bus_handle_t s_i2c_bus = NULL;  /* Bus I2C — BSP sở hữu (tạm thời đến Phase C) */
static uint8_t s_out_shadow = 0xFF;   /* 0xFF = tất cả inactive (active-low) */
static SemaphoreHandle_t s_do_mutex = NULL;

esp_err_t bsp_do_init(void)
{
    s_do_mutex = xSemaphoreCreateMutex();
    if (s_do_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create DO mutex");
        return ESP_ERR_NO_MEM;
    }

    /*
     * BƯỚC 1 — Tạo bus I2C master.
     *   - i2c_port: I2C_NUM_1 (chọn controller thứ 2 của ESP32-S3)
     *   - scl/sda: GPIO 41/42 theo sơ đồ board
     *   - glitch_ignore_cnt: 7 — bỏ qua nhiễu SCL ngắn
     *   - enable_internal_pullup: dùng điện trở kéo lên nội bộ cho SDA/SCL
     */
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
        return ret;
    }

    /* BƯỚC 2 — Gắn IC mở rộng (driver generic chỉ add device; không đụng
     * thanh ghi). Bus/address/clock/timeout là thông tin board truyền vào. */
    const tca9554_config_t tca_cfg = {
        .bus = s_i2c_bus,
        .address = BSP_TCA9554_ADDR,
        .clock_hz = BSP_I2C_FREQ_HZ,
        .timeout_ms = BSP_DO_EXPANDER_TIMEOUT_MS,
    };
    ret = tca9554_init(&s_expander, &tca_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init expander: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }

    /* BƯỚC 3 — Trình tự khởi tạo an toàn (không tạo output glitch):
     *   1) preset thanh ghi OUTPUT = 0xFF (mọi chân điện HIGH = đầu ra
     *      active-low đều OFF) — latch an toàn sẵn sàng TRƯỚC khi enable.
     *   2) CONFIG = 0x00 (tất cả chân thành output) — thời điểm sau khi
     *      latch đã chứa safe state, không output nào bị kích hoạt.
     *   3) s_out_shadow = 0xFF đồng bộ với phần cứng.
     * Không đảo thứ tự này. */
    ret = tca9554_write_outputs(&s_expander, 0xFF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to preset OUTPUT safe: %s", esp_err_to_name(ret));
        tca9554_deinit(&s_expander);
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }
    ret = tca9554_set_all_outputs(&s_expander);   /* CONFIG = 0x00 */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable outputs: %s", esp_err_to_name(ret));
        tca9554_deinit(&s_expander);
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }
    s_out_shadow = 0xFF;   /* Đồng bộ shadow với phần cứng (tất cả OFF) */

    ESP_LOGI(TAG, "Digital outputs ready (TCA9554, all OFF)");
    return ESP_OK;
}

esp_err_t bsp_do_write(bsp_do_channel_t channel, bool active)
{
    /* Kiểm tra kênh hợp lệ (0..7) trước khi tính toán bit. */
    if (channel < 0 || channel >= BSP_DO_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Cập nhật 1 bit trong shadow theo yêu cầu:
     *   - active = true  → xóa bit  (0)   → đầu ra kích hoạt (logic 0)
     *   - active = false → set bit  (1)   → đầu ra tắt (logic 1)
     * Không ghi trực tiếp 1 kênh mà thay vào toàn bộ shadow bên dưới. */
    if (xSemaphoreTake(s_do_mutex, pdMS_TO_TICKS(BSP_DO_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "DO mutex timeout; skipping channel %d write", channel + 1);
        return ESP_ERR_TIMEOUT;
    }
    if (active) s_out_shadow &= ~(1u << channel);
    else s_out_shadow |= (1u << channel);
    esp_err_t ret = tca9554_write_outputs(&s_expander, s_out_shadow);
    xSemaphoreGive(s_do_mutex);
    return ret;
}

esp_err_t bsp_do_write_mask(uint8_t active_mask)
{
    /* Đảo bitmask để ra shadow thực: bit set = active → xóa bit đó trong
     * shadow (active-low). Ví dụ: active_mask=0b00000001 → shadow=0b11111110. */
    if (xSemaphoreTake(s_do_mutex, pdMS_TO_TICKS(BSP_DO_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "DO mutex timeout; skipping mask write");
        return ESP_ERR_TIMEOUT;
    }
    s_out_shadow = (uint8_t)(~active_mask);
    esp_err_t ret = tca9554_write_outputs(&s_expander, s_out_shadow);
    xSemaphoreGive(s_do_mutex);
    return ret;
}

uint8_t bsp_do_get_shadow(void)
{
    /* Trả về trạng thái điện thực tế (0=ON, 1=OFF trên từng chân). */
    uint8_t shadow = s_out_shadow;
    if (s_do_mutex != NULL &&
        xSemaphoreTake(s_do_mutex, pdMS_TO_TICKS(BSP_DO_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        shadow = s_out_shadow;
        xSemaphoreGive(s_do_mutex);
    }
    return shadow;
}

uint8_t bsp_do_get_active_mask(void)
{
    /* Đảo lại shadow để trả về "bit set = đầu ra đang hoạt động".
     * Đây là dạng dễ dùng cho logic ứng dụng (1 = kích hoạt). */
    return (uint8_t)~bsp_do_get_shadow();
}

esp_err_t bsp_do_all_off(void)
{
    /* Viết tắt: tắt hết = ghi active_mask 0 (không đầu ra nào hoạt động). */
    return bsp_do_write_mask(0x00);
}
