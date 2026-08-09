/**
 * @file    nvs_storage.h
 * @brief   Lifecycle facade của NVS cho CallBox.
 *
 *          Sau Phase E.2.1, module này CHỈ còn chức năng lifecycle:
 *            - nvs_storage_init(): khởi tạo NVS flash (gọi một lần khi boot)
 *            - nvs_storage_erase_all(): xóa toàn bộ dữ liệu NVS
 *
 *          Mọi persistence dữ liệu đã được tách sang các module riêng:
 *            - callbox_config_store — cấu hình Wi-Fi/MQTT/callbox + migration
 *            - sequence_store — seq_num (high-watermark tin nhắn MQTT)
 *          Schema (namespace + key) dùng chung nằm trong
 *          callbox_storage_schema.h — không duplicate literal schema.
 *
 *          Module này KHÔNG gọi trực tiếp nvs.h/nvs_flash.h — mọi provider
 *          mechanics đi qua platform_nvs.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 2.0.0
 * @date    2026
 *
 * @see     platform_nvs.h — generic NVS provider
 * @see     callbox_config_store.h — persistence cấu hình
 * @see     sequence_store.h — persistence seq_num
 * @see     callbox_storage_schema.h — schema constants (product internal)
 */
#ifndef _NVS_STORAGE_H_
#define _NVS_STORAGE_H_

#include "esp_err.h"

/**
 * @brief Khởi tạo lưu trữ NVS (gọi một lần duy nhất khi boot).
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief Xóa toàn bộ dữ liệu NVS (factory reset mức flash).
 */
esp_err_t nvs_storage_erase_all(void);

#endif // _NVS_STORAGE_H_
