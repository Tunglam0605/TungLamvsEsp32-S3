/**
 * @file    callbox_config_store.h
 * @brief   CallBox configuration persistence + migration (product layer).
 *
 *          Module này sở hữu toàn bộ persistence của Config_t: namespace
 *          "callbox", schema/config keys, chính sách profile Wi-Fi và mọi
 *          migration (web_pass, legacy WiFi, mqtt_tls...). Provider thấp tầng
 *          là platform_nvs — module này KHÔNG gọi nvs.h/nvs_flash.h.
 *
 *          ═══ LOAD/SET ═══
 *          - callbox_config_store_load(): đọc từng key; key thiếu giữ nguyên
 *            default caller đã đặt trong Config_t. Load có thể GHI (migration
 *            web_pass) nên mở namespace ở chế độ read/write.
 *          - callbox_config_store_save(): một transaction — mở 1 lần, set mọi
 *            key, commit 1 lần nếu mọi set thành công, đóng 1 lần.
 *
 *          ═══ PROFILE HELPERS ═══
 *          config_add/find/remove_wifi_profile thao tác trực tiếp trên
 *          Config_t (thường là g_config) trước khi lưu bằng save.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     platform_nvs.h — generic NVS provider
 * @see     queues.h — Config_t
 */
#ifndef CALLBOX_CONFIG_STORE_H
#define CALLBOX_CONFIG_STORE_H

#include "queues.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Đọc cấu hình Wi-Fi/MQTT/callbox, giữ lại các giá trị mặc định do
 *        người gọi cung cấp cho các key chưa được lưu.
 */
esp_err_t callbox_config_store_load(Config_t *config);

/** @brief Lưu vĩnh viễn cấu hình Wi-Fi/MQTT/callbox. */
esp_err_t callbox_config_store_save(const Config_t *config);

/** Thêm mạng Wi-Fi vào danh sách nhớ, đưa nó lên ưu tiên cao nhất. */
void config_add_wifi_profile(Config_t *config, const char *ssid, const char *password);

/** Tìm mật khẩu đã nhớ cho một SSID. */
bool config_find_wifi_password(const Config_t *config, const char *ssid,
                              char *password, size_t password_size);

/** Xóa một mạng Wi-Fi đã nhớ. Trả về true khi nó từng tồn tại. */
bool config_remove_wifi_profile(Config_t *config, const char *ssid);

#endif /* CALLBOX_CONFIG_STORE_H */
