/**
 * @file time_sync.c
 * @brief Giữ RTC của ESP32 đồng bộ cho hoạt động MQTT qua TCP và TLS.
 *
 *          Đây là ADAPTER của sản phẩm: nó biết Config_t / g_config và mọi
 *          policy của CallBox (máy chủ SNTP mặc định, timezone ICT-7, mốc
 *          epoch hợp lệ). Mọi chi tiết ESP-IDF SNTP (esp_sntp_*) nằm ở
 *          component platform_time bên dưới.
 *
 *          Ánh xạ:
 *            Config_t / g_config
 *                  │  (mapping + default policy)
 *                  ▼
 *            platform_time_config_t
 *                  │
 *                  ▼
 *            platform_time (SNTP + timezone)
 */
#include "time_sync.h"

#include "esp_log.h"
#include "platform_time.h"

#include <time.h>

static const char *TAG = "TIME_SYNC";

/* Mốc epoch tối thiểu coi là "thời gian hợp lệ": 2024-01-01 00:00:00 UTC.
 * Đây là policy của CallBox, platform chỉ so sánh với ngưỡng caller truyền. */
#define CALLBOX_MIN_VALID_EPOCH 1704067200

/* Ánh xạ Config_t (g_config / config portal) sang cấu hình platform.
 * Máy chủ mặc định là policy của sản phẩm — không nằm trong platform. */
static platform_time_config_t time_sync_map_config(const Config_t *config)
{
    const char *primary = config && config->sntp_primary[0] ? config->sntp_primary : "pool.ntp.org";
    const char *fallback = config && config->sntp_fallback[0] ? config->sntp_fallback : "time.google.com";

    return (platform_time_config_t){
        .primary_server = primary,
        .fallback_server = fallback,
        .timezone = "ICT-7",
    };
}

void time_sync_init(void)
{
    /* Hiển thị cục bộ theo giờ ICT; MQTT vẫn truyền thời gian Unix UTC epoch.
     * Policy timezone nằm ở adapter; platform áp dụng qua setenv/tzset. */
    platform_time_config_t cfg = time_sync_map_config(&g_config);
    esp_err_t ret = platform_time_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(ret));
    }
}

bool time_sync_is_valid(void)
{
    return platform_time_is_valid(CALLBOX_MIN_VALID_EPOCH);
}

void time_sync_reconfigure(const Config_t *config)
{
    if (!config) return;

    platform_time_config_t cfg = time_sync_map_config(config);
    esp_err_t ret = platform_time_reconfigure(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reconfigure failed: %s", esp_err_to_name(ret));
    }
}
