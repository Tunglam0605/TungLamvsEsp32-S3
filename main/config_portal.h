/**
 * @file    config_portal.h
 * @brief   Portal cấu hình web trên SoftAP để thiết lập Wi-Fi/MQTT/ID callbox.
 *
 *          Trang web nhẹ được nhúng trong firmware (HTML/CSS/JS inline,
 *          không dùng CDN). Chạy trên địa chỉ SoftAP CALLBOX_AP_IP_ADDR
 *          khi bật gọi một lần — config_portal_start() trong app_main.
 *
 *          ═══ NHỮNG GÌ PORTAL LÀM ═══
 *          1) Quét kênh Wi-Fi (dùng wifi_scan_lock để đồng bộ hóa)
 *          2) Sửa: Wifi SSID/pass, MQTT broker/port/user/pass, callbox ID
 *          3) Áp dụng cấu hình không cần khởi động lại (wifi_apply_config)
 *          4) Phiên cấu hình chỉ có trong thời gian browser hoạt động
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     wifi_init.h — SoftAP và địa chỉ IP
 * @see     nvs_storage.h — lưu cấu hình người dùng
 */
#ifndef CALLBOX_CONFIG_PORTAL_H
#define CALLBOX_CONFIG_PORTAL_H

#include "esp_err.h"
#include "queues.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start the local phone configuration page on the AP+STA web server. */
esp_err_t config_portal_start(Config_t *config);

/** True while a browser configuration session lease is active. */
bool config_portal_session_active(void);

#ifdef __cplusplus
}
#endif

#endif /* CALLBOX_CONFIG_PORTAL_H */
