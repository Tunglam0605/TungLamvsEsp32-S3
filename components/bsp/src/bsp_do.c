/**
 * @file    bsp_do.c
 * @brief   Triển khai driver đầu ra số (8DO) cho board Waveshare 8DI/8DO.
 *
 *          8 đầu ra được điều khiển qua IC mở rộng TCA9554PWR trên I2C.
 *          Trạng thái thực tế được lưu trong shadow register (s_out_shadow)
 *          để đọc lại mà không cần giao dịch I2C.
 *
 *          ═══ OWNERSHIP (SAU PHASE C) ═══
 *          BSP_DO consume bus I2C do board sở hữu (bsp_i2c) — KHÔNG tạo/xóa
 *          bus. BSP_DO sở hữu: TCA device instance, address 0x20, tốc độ
 *          400 kHz của device, timeout, mutex, shadow, active-low policy,
 *          safe OUTPUT 0xFF, all-output CONFIG policy.
 *
 * @note    Đầu ra active-low: shadow bit = 1 = điện HIGH = đầu ra TẮT (OFF);
 *          shadow bit = 0 = điện LOW = đầu ra KÍCH HOẠT (ON).
 *          Shadow khởi tạo 0xFF = tất cả OFF.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_do.h — API đầu ra số
 * @see     bsp_i2c.h — bus I2C board (private) — bus này consume
 * @see     tca9554.h — driver TCA9554 cấp thấp
 */
#include "bsp_do.h"
#include "bsp_board.h"
#include "bsp_i2c.h"
#include "tca9554.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "BSP_DO";

/*
 * Biến toàn cục: instance expander + shadow register của đầu ra.
 * Đầu ra active-low: shadow bit 1 = điện HIGH = TẮT; bit 0 = điện LOW = ON.
 */
/* Địa chỉ 7-bit của TCA9554 trên board này (A0/A1 nối GND). */
#define BSP_TCA9554_ADDR    0x20
/* Tần số SCL của TCA9554 (device-level, KHÔNG phải bus-level). */
#define BSP_DO_EXPANDER_FREQ_HZ 400000
/* Thời gian chờ tối đa cho giao dịch I2C với expander (ms). */
#define BSP_DO_EXPANDER_TIMEOUT_MS 100U
#define BSP_DO_MUTEX_TIMEOUT_MS 50U

static tca9554_t s_expander;                 /* Instance TCA: chỉ handle device + timeout (zero-init static) */
static uint8_t s_out_shadow = 0xFF;   /* 0xFF = tất cả inactive (active-low) */
static SemaphoreHandle_t s_do_mutex = NULL;

/*
 * Dọn dọn khi init thất bại (chỉ dùng trong bsp_do_init).
 * SAU PHASE C: bus I2C thuộc board layer (bsp_i2c) — bsp_do KHÔNG xóa bus.
 *
 * - Nếu device detach lỗi: log lỗi, GIỮ nguyên handle (driver đã đảm bảo).
 * - Mutex luôn được xóa khi đã được tạo — không được leak.
 * - Shadow reset 0xFF (all-OFF an toàn).
 * - Primary error của caller luôn được giữ nguyên; lỗi cleanup chỉ log.
 */
static void bsp_do_cleanup_partial_init(void)
{
    /* 1) TCA device — detach nếu chính bsp_do đã attach (không phải bus). */
    if (s_expander.dev != NULL) {
        if (tca9554_deinit(&s_expander) != ESP_OK) {
            ESP_LOGE(TAG, "TCA device detach failed during init cleanup; "
                          "keeping device handle (board layer owns the bus)");
        }
    }

    /* 2) Mutex — không được leak khi init thất bại sau khi tạo. */
    if (s_do_mutex != NULL) {
        vSemaphoreDelete(s_do_mutex);
        s_do_mutex = NULL;
    }

    /* 3) Reset trạng thái logic: luôn để về all-OFF an toàn. */
    s_out_shadow = 0xFF;
}

esp_err_t bsp_do_init(void)
{
    s_do_mutex = xSemaphoreCreateMutex();
    if (s_do_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create DO mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Lấy bus I2C board do board layer sở hữu (bsp_i2c). BSP_DO KHÔNG tạo
     * bus — đây là tài nguyên của BOARD, phải được bsp_board_init khởi tạo
     * trước (order: DI → bsp_i2c → DO → buzzer). */
    i2c_master_bus_handle_t bus = bsp_i2c_get_bus();
    if (bus == NULL) {
        ESP_LOGE(TAG, "Board I2C bus not initialized (bsp_i2c_init missing?)");
        bsp_do_cleanup_partial_init();
        return ESP_ERR_INVALID_STATE;
    }

    /* BƯỚC 1 — Gắn IC mở rộng (driver generic chỉ add device; không đụng
     * thanh ghi). TCA address 0x20 + SCL 400 kHz + timeout là thông tin
     * của BOARD/DEVICE, do BSP_DO truyền vào. */
    const tca9554_config_t tca_cfg = {
        .bus = bus,
        .address = BSP_TCA9554_ADDR,
        .clock_hz = BSP_DO_EXPANDER_FREQ_HZ,
        .timeout_ms = BSP_DO_EXPANDER_TIMEOUT_MS,
    };
    esp_err_t ret = tca9554_init(&s_expander, &tca_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init expander: %s", esp_err_to_name(ret));
        bsp_do_cleanup_partial_init();
        return ret;
    }

    /* BƯỚC 2 — Trình tự khởi tạo an toàn (không tạo output glitch):
     *   1) preset OUTPUT = 0xFF (mọi chân điện HIGH = đầu ra active-low
     *      đều OFF) — latch an toàn sẵn sàng TRƯỚC khi enable.
     *   2) CONFIG = 0x00 (tất cả chân thành output) — latch đã chứa safe
     *      state, không output nào bị kích hoạt.
     *   3) s_out_shadow = 0xFF đồng bộ với phần cứng.
     * Không đảo thứ tự này. */
    ret = tca9554_write_outputs(&s_expander, 0xFF);   /* preset OUTPUT latch */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to preset OUTPUT safe: %s", esp_err_to_name(ret));
        bsp_do_cleanup_partial_init();
        return ret;
    }
    ret = tca9554_set_all_outputs(&s_expander);       /* CONFIG = 0x00 */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable outputs: %s", esp_err_to_name(ret));
        bsp_do_cleanup_partial_init();
        return ret;
    }
    s_out_shadow = 0xFF;   /* Đồng bộ shadow với phần cứng (tất cả OFF) */

    ESP_LOGI(TAG, "Hardware outputs ready (TCA9554, all OFF)");
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
