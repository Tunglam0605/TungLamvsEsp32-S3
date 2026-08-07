/**
 * @file    nvs_storage.h
 * @brief   Lưu/đọc cấu hình và số thứ tự (seq_num) trong NVS.
 *
 *          Sử dụng namespace "callbox" của NVS flash để giữ dữ liệu bền
 *          qua các lần reset/mất điện.
 *
 *          ═══ CÁC KEY CHÍNH ═══
 *          ┌────────────────┬──────────────────────────────────────┐
 *          │ wifi_ssid/pass │ Mạng Wi-Fi chính                     │
 *          │ wifiX_ssid/pass│ 5 profile Wi-Fi nhớ (wifi0..wifi4)  │
 *          │ mqtt_broker    │ Địa chỉ broker                       │
 *          │ mqtt_port      │ Cổng MQTT (1883/1884)               │
 *          │ callbox_id     │ ID logic của callbox                 │
 *          │ seq_num        │ Số thứ tự tin nhắn (bền)            │
 *          │ web_pass       │ Mật khẩu portal (STA/Ethernet)      │
 *          └────────────────┴──────────────────────────────────────┘
 *
 * @note    Các hàm config_add/find/remove_wifi_profile thao tác trực tiếp
 *          trên Config_t (thường là g_config) trước khi lưu bằng nvs_save_config.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     queues.h — Config_t
 * @see     config_portal.c — đọc/ghi cấu hình từ web
 */
#ifndef _NVS_STORAGE_H_
#define _NVS_STORAGE_H_

#include "esp_err.h"
#include "queues.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize NVS storage
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief Save sequence number to NVS
 */
esp_err_t nvs_save_seq_num(uint32_t seq);

/**
 * @brief Load sequence number from NVS
 */
esp_err_t nvs_load_seq_num(uint32_t *seq);

/**
 * @brief Load Wi-Fi/MQTT/callbox settings, preserving caller-provided defaults
 *        for keys that have not been saved yet.
 */
esp_err_t nvs_load_config(Config_t *config);

/** @brief Persist Wi-Fi/MQTT/callbox settings and sequence number. */
esp_err_t nvs_save_config(const Config_t *config);

/** Add a Wi-Fi network to the remembered list, moving it to highest priority. */
void config_add_wifi_profile(Config_t *config, const char *ssid, const char *password);

/** Look up a remembered password for an SSID. */
bool config_find_wifi_password(const Config_t *config, const char *ssid,
                              char *password, size_t password_size);

/** Remove one remembered Wi-Fi network. Returns true when it existed. */
bool config_remove_wifi_profile(Config_t *config, const char *ssid);

/**
 * @brief Erase all NVS data
 */
esp_err_t nvs_storage_erase_all(void);

#endif // _NVS_STORAGE_H_
