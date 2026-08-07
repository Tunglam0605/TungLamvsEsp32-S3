/**
 * @file    wifi_init.h
 * @brief   Khởi tạo Wi-Fi ở chế độ APSTA (Station + SoftAP cấu hình).
 *
 *          Boot là APSTA: STA quét và chọn mạng nhớ mạnh nhất, đồng thời
 *          SoftAP "CALLBOX-<id>" mở ngay để người dùng cấu hình bằng điện
 *          thoại (portal web tại CALLBOX_AP_IP_ADDR).
 *
 *          ═══ SOFTAP ═══
 *          SSID:   CALLBOX-<id>     (đặt qua wifi_init_sta_profiles)
 *          Mật khẩu: trùng SSID AP (ví dụ CALLBOX-02)
 *          IP:     192.168.65.204/24  (CALLBOX_AP_IP_ADDR)
 *
 * @note    network_is_connected() = STA có IP Hoặc W5500 Ethernet có IP.
 *          AP tự bật lại khi mất STA — đây là đường khôi phục của Portal.
 *          Các hàm wifi_scan_lock/unlock dùng chung việc quét giữa portal
 *          và trình chọn profile nền.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     config_portal.h — web cấu hình trên AP
 * @see     nvs_storage.h — lưu profile Wi-Fi
 * @see     mqtt_client.c — phụ thuộc vào network_is_connected()
 */
#ifndef _WIFI_INIT_H_
#define _WIFI_INIT_H_

#include "esp_event.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "queues.h"

/* Fixed address of the local recovery/configuration SoftAP. */
#define CALLBOX_AP_IP_ADDR "192.168.65.204"
#define CALLBOX_AP_NETMASK "255.255.255.0"

typedef void (*wifi_config_ap_callback_t)(void);

typedef struct {
    bool connected;
    char ssid[33];
    int8_t rssi;
    char ip[16];
    char gateway[16];
} wifi_sta_status_t;

/**
 * @brief Initialize WiFi in Station mode
 * @param ssid: WiFi network SSID
 * @param password: WiFi password
 */
void wifi_init_sta(const char *ssid, const char *password);

/** Legacy helper: start one station profile and enable the AP immediately. */
esp_err_t wifi_init_apsta(const char *ssid, const char *password,
                          const char *ap_ssid, const char *ap_password);

/** Start station profiles and the local configuration AP immediately. */
esp_err_t wifi_init_sta_profiles(const Config_t *config,
                                 const char *ap_ssid, const char *ap_password);

/** Apply a newly saved station profile without rebooting the device. */
esp_err_t wifi_apply_config(const Config_t *config);

/** Register a callback invoked when the local configuration AP starts. */
void wifi_set_config_ap_callback(wifi_config_ap_callback_t callback);

/**
 * @brief WiFi event handler
 */
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                       int32_t event_id, void *event_data);

/**
 * @brief Check if WiFi is connected
 * @return 1 if connected, 0 if disconnected
 */
uint8_t wifi_is_connected(void);

/** Snapshot current STA identity and IPv4 information for diagnostics UI. */
void wifi_get_sta_status(wifi_sta_status_t *status);

/** True when either the Wi-Fi station or W5500 Ethernet has an IP address. */
uint8_t network_is_connected(void);

/** Serialize Wi-Fi scans from the portal and the background profile selector. */
bool wifi_scan_lock(uint32_t timeout_ms);
void wifi_scan_unlock(void);

/** Local configuration AP state. */
bool wifi_ap_is_active(void);
uint8_t wifi_ap_client_count(void);

/** Stop the local AP when the application has confirmed it is idle. */
esp_err_t wifi_stop_config_ap(void);

/**
 * @brief Toggle the operator-requested rescue AP latch.
 *
 * When enabled, the AP stays available even while STA is healthy.  Disabling
 * the latch restores the normal policy: the AP is stopped only when STA is
 * healthy and no configuration client is connected.
 */
esp_err_t wifi_toggle_rescue_ap(bool *enabled);

/** @brief True when the local AP has been explicitly requested by long-press. */
bool wifi_rescue_ap_is_enabled(void);

#endif // _WIFI_INIT_H_
