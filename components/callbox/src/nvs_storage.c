/**
 * @file    nvs_storage.c
 * @brief   Lifecycle facade của NVS cho CallBox.
 *
 *          Sau Phase E.2.1, module này CHỈ còn chức năng lifecycle:
 *            - nvs_storage_init(): khởi tạo NVS flash (delegate platform_nvs_init)
 *            - nvs_storage_erase_all(): xóa toàn bộ partition NVS
 *          Mọi persistence dữ liệu đã được tách sang:
 *            - callbox_config_store — cấu hình + migration + profile helper
 *            - sequence_store — seq_num (high-watermark tin nhắn)
 *          Schema dùng chung nằm trong callbox_storage_schema.h.
 *
 *          ═══ PHÂN TẦNG (SAU PHASE E.2.1) ═══
 *              CallBox Application
 *                      │
 *                      ▼
 *              nvs_storage.c         ← MODULE NÀY (lifecycle facade)
 *                      │  platform_nvs_init() / platform_nvs_erase_all()
 *                      ▼
 *              platform_nvs          ← generic provider (component nền tảng)
 *                      │
 *                      ▼
 *              ESP-IDF NVS
 *
 *          Module này KHÔNG gọi trực tiếp nvs.h / nvs_flash.h — mọi provider
 *          mechanics đi qua platform_nvs.
 *
 *          Warning: gọi nvs_storage_init() một lần duy nhất; không gọi lại
 *          trong task — module này xử lý tại app_main.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 2.0.0
 * @date    2026
 *
 * @see     nvs_storage.h — API
 * @see     callbox_config_store.h — persistence cấu hình
 * @see     sequence_store.h — persistence seq_num
 * @see     callbox_storage_schema.h — schema constants (product internal)
 */
#include "nvs_storage.h"
#include "platform_nvs.h"
#include "esp_log.h"

static const char *TAG = "NVS_STORAGE";

esp_err_t nvs_storage_init(void)
{
    /* Lifecycle NVS flash do platform_nvs sở hữu (kể cả recovery erase khi
     * partition bị hỏng — xem platform_nvs.h). */
    return platform_nvs_init();
}

esp_err_t nvs_storage_erase_all(void)
{
    ESP_LOGW(TAG, "Erasing all NVS data");
    return platform_nvs_erase_all();
}
