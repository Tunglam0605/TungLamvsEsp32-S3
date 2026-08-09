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
 * @see     callbox_config_store.h — lưu profile Wi-Fi
 * @see     mqtt_client.c — phụ thuộc vào network_is_connected()
 */
#ifndef _WIFI_INIT_H_
#define _WIFI_INIT_H_

#include "esp_event.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "queues.h"

/* Địa chỉ cố định của SoftAP khôi phục/cấu hình cục bộ. */
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
 * @brief Khởi tạo Wi-Fi ở chế độ Station
 * @param ssid: SSID mạng Wi-Fi
 * @param password: Mật khẩu Wi-Fi
 */
void wifi_init_sta(const char *ssid, const char *password);

/** Hỗ trợ cũ: bắt đầu một profile station và bật AP ngay lập tức. */
esp_err_t wifi_init_apsta(const char *ssid, const char *password,
                          const char *ap_ssid, const char *ap_password);

/** Bắt đầu các profile station và AP cấu hình cục bộ ngay. */
esp_err_t wifi_init_sta_profiles(const Config_t *config,
                                 const char *ap_ssid, const char *ap_password);

/** Áp dụng profile station vừa lưu mà không cần khởi động lại thiết bị. */
esp_err_t wifi_apply_config(const Config_t *config);

/** Đăng ký callback được gọi khi AP cấu hình cục bộ khởi động. */
void wifi_set_config_ap_callback(wifi_config_ap_callback_t callback);

/**
 * @brief Xử lý sự kiện Wi-Fi
 */
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                       int32_t event_id, void *event_data);

/**
 * @brief Kiểm tra Wi-Fi đã kết nối chưa
 * @return 1 nếu đã kết nối, 0 nếu chưa
 */
uint8_t wifi_is_connected(void);

/** Chụp nhanh danh tính STA hiện tại và thông tin IPv4 cho UI chẩn đoán. */
void wifi_get_sta_status(wifi_sta_status_t *status);

/** True khi station Wi-Fi hoặc W5500 Ethernet có địa chỉ IP. */
uint8_t network_is_connected(void);

/** Tuần tự hóa quét Wi-Fi giữa portal và trình chọn profile nền. */
bool wifi_scan_lock(uint32_t timeout_ms);
void wifi_scan_unlock(void);

/** Trạng thái AP cấu hình cục bộ. */
bool wifi_ap_is_active(void);
uint8_t wifi_ap_client_count(void);

/** Dừng AP cục bộ khi ứng dụng xác nhận đã nhàn rỗi. */
esp_err_t wifi_stop_config_ap(void);

/**
 * @brief Bật/tắt latch Rescue AP do người vận hành yêu cầu.
 *
 * Khi bật, AP vẫn khả dụng ngay cả khi STA khỏe mạnh. Tắt latch sẽ khôi
 * phục chính sách bình thường: AP chỉ dừng khi STA khỏe và không có client
 * cấu hình đang kết nối.
 */
esp_err_t wifi_toggle_rescue_ap(bool *enabled);

/** @brief True khi AP cục bộ đã được yêu cầu rõ ràng bằng nhấn giữ. */
bool wifi_rescue_ap_is_enabled(void);

#endif // _WIFI_INIT_H_
