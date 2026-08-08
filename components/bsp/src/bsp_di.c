/**
 * @file    bsp_di.c
 * @brief   Triển khai driver đầu vào số (8DI) cho board Waveshare 8DI/8DO.
 *
 *          Các đầu vào được cấu hình là GPIO input có pull-up, opto cách ly,
 *          active-low (mức 0 = kích hoạt). Mỗi kênh BSP_DI_x ánh xạ tới một
 *          GPIO theo bảng trong bsp_di.h.
 *
 *          ═══ BẢNG CHÂN (index = bsp_di_channel_t) ═══
 *          BSP_DI_1..8 → GPIO_NUM_4 .. GPIO_NUM_11
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_di.h — API và bảng chân
 * @see     bsp_board.c — gọi bsp_di_init() trong khởi tạo board
 * @see     bsp_internal.h — bsp_di_init là init private (chỉ board gọi)
 */
#include "bsp_di.h"
#include "bsp_internal.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BSP_DI";

/* State initialized: TRUE chỉ khi gpio_config() thành công. Runtime API
 * (bsp_di_read / bsp_di_read_all) trả "inactive" khi chưa init — không đọc
 * GPIO trên chân chưa cấu hình. */
static bool s_initialized = false;

/*
 * Bảng ánh xạ kênh → GPIO (index chính là giá trị bsp_di_channel_t,
 * nên BSP_DI_1 nằm ở phần tử 0, BSP_DI_8 nằm ở phần tử 7).
 * Đầu vào opto cách ly, active-low (cấp 0 = kích hoạt).
 */
static const gpio_num_t s_di_gpio[BSP_DI_COUNT] = {
    GPIO_NUM_4,  /* BSP_DI_1 */
    GPIO_NUM_5,  /* BSP_DI_2 */
    GPIO_NUM_6,  /* BSP_DI_3 */
    GPIO_NUM_7,  /* BSP_DI_4 */
    GPIO_NUM_8,  /* BSP_DI_5 */
    GPIO_NUM_9,  /* BSP_DI_6 */
    GPIO_NUM_10, /* BSP_DI_7 */
    GPIO_NUM_11, /* BSP_DI_8 */
};

esp_err_t bsp_di_init(void)
{
    /* Idempotent: GPIO là board setup — đã cấu hình thành công rồi thì
     * không cấu hình lại (board composition gọi 1 lần; lặp lại là no-op). */
    if (s_initialized) {
        return ESP_OK;
    }

    /*
     * BƯỚC 1 — Dựng bitmask các chân sẽ cấu hình.
     * Mỗi bit tương ứng một GPIO: (1ULL << gpio). Gom cả 8 chân vào 1 lệnh
     * gpio_config để tiết kiệm thời gian cấu hình từng chân riêng lẻ.
     */
    uint64_t mask = 0;
    for (int i = 0; i < BSP_DI_COUNT; i++) {
        mask |= (1ULL << s_di_gpio[i]);
    }

    /*
     * BƯỚC 2 — Cấu hình GPIO làm input có pull-up.
     *   - mode: INPUT (đọc mức từ opto)
     *   - pull_up_en: bật pull-up nội — mặc định mức HIGH khi không nối gì
     *   - pull_down: tắt (không cần vì đã pull-up)
     *   - intr_type: không dùng ngắt; BSP đọc kiểu poll (10Hz ở io_handler)
     */
    gpio_config_t cfg = {
        .pin_bit_mask  = mask,
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        s_initialized = false;   /* Không mark initialized khi failure */
        return ret;
    }

    s_initialized = true;   /* Chỉ set true khi GPIO config thành công */
    ESP_LOGI(TAG, "Digital inputs configured (GPIO4-11)");
    return ESP_OK;
}

bool bsp_di_read(bsp_di_channel_t channel)
{
    /* Chưa init → false: không input nào được báo active khi phần cứng
     * chưa sẵn sàng (không đọc GPIO trên chân chưa cấu hình). */
    if (!s_initialized) return false;
    /* Chống truy cập ngoài phạm vi kênh hợp lệ (0..7). */
    if (channel < 0 || channel >= BSP_DI_COUNT) return false;
    /* Active-low: đầu vào được kích hoạt (energized) → GPIO đọc mức LOW.
     * Vì vậy "active" = (mức GPIO == 0). */
    return gpio_get_level(s_di_gpio[channel]) == 0;
}

uint8_t bsp_di_read_all(void)
{
    uint8_t mask = 0;
    /* Đọc tuần tự từng kênh rồi gom vào một bitmask 8-bit.
     * bit 0 = BSP_DI_1, bit 7 = BSP_DI_8. */
    for (int i = 0; i < BSP_DI_COUNT; i++) {
        if (bsp_di_read((bsp_di_channel_t)i)) {
            mask |= (1u << i);
        }
    }
    return mask;
}