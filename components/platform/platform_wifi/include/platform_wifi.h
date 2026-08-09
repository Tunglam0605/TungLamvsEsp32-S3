/**
 * @file    platform_wifi.h
 * @brief   Provider Wi-Fi nền tảng: driver + netif + event bridge của ESP-IDF.
 *
 *          Component platform_wifi sở hữu toàn bộ provider mechanics của
 *          ESP-IDF Wi-Fi (esp_wifi_*, esp_netif_* Wi-Fi STA/AP, WIFI_EVENT /
 *          IP_EVENT). Caller (product policy, vd: wifi_init) chỉ cung cấp
 *          cấu hình + đăng ký callback — không cần biết bất kỳ chi tiết
 *          ESP-IDF Wi-Fi nào.
 *
 *          ═══ ĐƠN VỊ SỞ HỮU ═══
 *          - Driver Wi-Fi singleton + khởi tạo netif STA/AP + event loop
 *          - ESP-IDF event handler (bridge → platform_wifi_event_t)
 *          - Trạng thái factual: STA có IP, AP active, AP client count
 *          - Raw STA configure/connect/disconnect, raw AP start/stop
 *          - Raw scan (blocking, capacity do caller cung cấp)
 *          - Raw STA status (SSID/RSSI/IP/gateway)
 *
 *          ═══ NOT SỞ HỮU (POLICY LÀ CỦA CALLER) ═══
 *          - Không chọn profile, không biết RSSI-strongest selection
 *          - Không quét phối hợp / scan lock (mutex của product)
 *          - Không recovery AP, không Rescue AP, không retry policy
 *          - Không identity (SSID/password AP do application build)
 *          - Không Config_t / WifiProfile_t / CallBox / business
 *          - Không BSP, Ethernet, MQTT, persistence
 *
 *          ═══ STRING LIFETIME ═══
 *          Mọi chuỗi cấu hình (AP SSID/password, IP tĩnh STA) được SAO CHÉP
 *          vào storage tĩnh của platform — không bao giờ giữ con trỏ tới
 *          stack/temp của caller sau khi hàm trả về.
 *
 *          ═══ ERROR MAPPING ═══
 *          Platform map một số lỗi Wi-Fi sang mã chung để caller không cần
 *          include esp_wifi.h:
 *            - ESP_ERR_WIFI_STATE  → ESP_ERR_INVALID_STATE
 *              (STA đang hoàn tất kết nối / driver đang bận)
 *          Các lỗi ESP-IDF khác được trả nguyên về caller và được document
 *          tại từng hàm.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     main/wifi_init.c — product policy (profiles, rescue, scan lock)
 */
#ifndef PLATFORM_WIFI_H
#define PLATFORM_WIFI_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cấu hình mạng STA: DHCP hoặc IP tĩnh (do caller cung cấp). */
typedef struct {
    bool dhcp;             /**< true = DHCP; false = IP tĩnh (các trường ip/netmask/gateway/dns) */
    const char *ip;        /**< IP tĩnh (bỏ qua nếu dhcp = true) */
    const char *netmask;   /**< Netmask tĩnh (bỏ qua nếu dhcp = true) */
    const char *gateway;   /**< Gateway tĩnh (bỏ qua nếu dhcp = true) */
    const char *dns;       /**< DNS chính tĩnh (bỏ qua nếu dhcp = true) */
} platform_wifi_sta_network_config_t;

/** Cấu hình SoftAP (do caller cung cấp; identity là policy của caller). */
typedef struct {
    const char *ssid;      /**< SSID AP (sao chép vào storage của platform) */
    const char *password;  /**< Mật khẩu AP (sao chép vào storage của platform) */
    const char *ip;        /**< IP cố định của AP (vd "192.168.65.204") */
    const char *netmask;   /**< Netmask AP (vd "255.255.255.0") */
    uint8_t channel;       /**< Kênh AP */
    uint8_t max_clients;   /**< Số client AP tối đa */
} platform_wifi_ap_config_t;

/** Cấu hình quét (show_hidden + cửa sổ active scan). */
typedef struct {
    bool show_hidden;      /**< true = hiện mạng ẩn trong kết quả */
    uint16_t active_min_ms; /**< Active scan: thời gian tối thiểu mỗi kênh (ms) */
    uint16_t active_max_ms; /**< Active scan: thời gian tối đa mỗi kênh (ms) */
} platform_wifi_scan_config_t;

/**
 * @brief Phân loại bảo mật Wi-Fi trung tính với provider (từ authmode ESP-IDF).
 *
 *        ═══ JSON COMPATIBILITY ═══
 *        Giá trị số 0..17 CỐ Ý giữ nguyên định nghĩa wifi_auth_mode_t của
 *        ESP-IDF v6.1 (bao gồm placeholder/reserved 11..12, DPP 13, WPA3-
 *        Enterprise 14, WPA2/WPA3-Enterprise 15, WPA-Enterprise 16 và WIFI_
 *        AUTH_MAX 17) để caller (config_portal) xuất trực tiếp vào JSON
 *        "auth" với mã legacy portal không đổi.
 *        PLATFORM_WIFI_AUTH_UNMAPPED = 255 dành riêng cho giá trị provider
 *        ngoài hợp đồng 0..17 này (không thuộc firmware/phiên bản đang
 *        chạy).
 */
typedef enum {
    PLATFORM_WIFI_AUTH_OPEN = 0,             /**< Mạng mở (không bảo mật) */
    PLATFORM_WIFI_AUTH_WEP = 1,              /**< WEP */
    PLATFORM_WIFI_AUTH_WPA_PSK = 2,          /**< WPA-PSK */
    PLATFORM_WIFI_AUTH_WPA2_PSK = 3,         /**< WPA2-PSK */
    PLATFORM_WIFI_AUTH_WPA_WPA2_PSK = 4,     /**< WPA/WPA2 mixed */
    PLATFORM_WIFI_AUTH_ENTERPRISE = 5,       /**< WPA2-Enterprise */
    PLATFORM_WIFI_AUTH_WPA3_PSK = 6,         /**< WPA3-PSK */
    PLATFORM_WIFI_AUTH_WPA2_WPA3_PSK = 7,    /**< WPA2/WPA3 mixed */
    PLATFORM_WIFI_AUTH_WAPI_PSK = 8,         /**< WAPI-PSK */
    PLATFORM_WIFI_AUTH_OWE = 9,              /**< OWE (WPA3 Transition) */
    PLATFORM_WIFI_AUTH_WPA3_ENT_192 = 10,    /**< WPA3-Enterprise 192-bit */
    PLATFORM_WIFI_AUTH_RESERVED_11 = 11,     /**< Reserved 11 = WIFI_AUTH_DUMMY_1 v6.1 (placeholder, giữ mã legacy) */
    PLATFORM_WIFI_AUTH_RESERVED_12 = 12,     /**< Reserved 12 = WIFI_AUTH_DUMMY_2 v6.1 (placeholder, giữ mã legacy) */
    PLATFORM_WIFI_AUTH_DPP = 13,             /**< DPP (Device Provisioning Protocol) */
    PLATFORM_WIFI_AUTH_WPA3_ENTERPRISE = 14, /**< WPA3-Enterprise Only Mode */
    PLATFORM_WIFI_AUTH_WPA2_WPA3_ENTERPRISE = 15, /**< WPA3-Enterprise Transition Mode */
    PLATFORM_WIFI_AUTH_WPA_ENTERPRISE = 16,  /**< WPA-Enterprise security */
    PLATFORM_WIFI_AUTH_UNKNOWN = 17,         /**< WIFI_AUTH_MAX v6.1 (phân loại quét hợp lệ, giữ mã legacy) */
    PLATFORM_WIFI_AUTH_UNMAPPED = 255,       /**< Giá trị provider ngoài hợp đồng 0..17 (fallback an toàn) */
} platform_wifi_auth_mode_t;

/** Một mạng quan sát được trong kết quả quét (generic, không phải wifi_ap_record_t). */
typedef struct {
    char ssid[33];                    /**< SSID mạng (kết thúc null) */
    int8_t rssi;                      /**< RSSI dBm */
    platform_wifi_auth_mode_t auth_mode; /**< Phân loại bảo mật (xem enum trên) */
} platform_wifi_scan_record_t;

/** Trạng thái STA hiện tại cho UI chẩn đoán. */
typedef struct {
    bool connected;        /**< STA có IP chưa */
    char ssid[33];         /**< SSID AP đang kết nối */
    int8_t rssi;           /**< RSSI dBm */
    char ip[16];           /**< IP IPv4 dạng chuỗi */
    char gateway[16];      /**< Gateway IPv4 dạng chuỗi */
} platform_wifi_sta_status_t;

/**
 * @brief Sự kiện Wi-Fi trung tính với provider (bridge từ ESP-IDF events).
 *
 *        ESP-IDF event handler nằm trong platform_wifi; caller chỉ nhận các
 *        sự kiện generic này qua callback đăng ký ở platform_wifi_start_apsta.
 *
 * @note  Quét có thể chuyển mode AP-only → APSTA (xem platform_wifi_scan) —
 *        driver sẽ phát các event tương ứng (AP_STARTED/STOPPED...) và
 *        platform_wifi cập nhật trạng thái factual theo đó.
 */
typedef enum {
    PLATFORM_WIFI_EVENT_STA_STARTED,          /**< driver Wi-Fi STA bắt đầu */
    PLATFORM_WIFI_EVENT_STA_DISCONNECTED,     /**< STA mất kết nối */
    PLATFORM_WIFI_EVENT_STA_GOT_IP,           /**< STA nhận IP (thành công) */
    PLATFORM_WIFI_EVENT_AP_STARTED,           /**< SoftAP bắt đầu */
    PLATFORM_WIFI_EVENT_AP_STOPPED,           /**< SoftAP dừng */
    PLATFORM_WIFI_EVENT_AP_CLIENT_CONNECTED,  /**< một client AP kết nối */
    PLATFORM_WIFI_EVENT_AP_CLIENT_DISCONNECTED, /**< một client AP ngắt kết nối */
} platform_wifi_event_t;

/** Callback nhận event bridge (policy của caller). */
typedef void (*platform_wifi_event_callback_t)(platform_wifi_event_t event, void *context);

/**
 * @brief  Khởi tạo provider Wi-Fi và bật APSTA (STA + SoftAP).
 *
 *         Thực hiện: esp_netif_init / esp_event_loop_create_default (chấp
 *         nhận ESP_ERR_INVALID_STATE khi global service đã init — bắt buộc
 *         giữ thứ tự gọi sớm cho W5500 BSP), tạo netif STA+AP, esp_wifi_init,
 *         đăng ký ESP-IDF event handler, set mode APSTA và esp_wifi_start.
 *
 * @param  sta_network  Cấu hình mạng STA (DHCP/static; có thể NULL → DHCP).
 * @param  ap_config    Cấu hình SoftAP (SSID/password/IP; bắt buộc).
 * @param  callback     Callback event bridge (có thể NULL).
 * @param  context      Context truyền lại cho callback.
 * @return ESP_OK                 sẵn sàng (APSTA đang chạy)
 * @return ESP_ERR_INVALID_ARG    ap_config NULL
 * @return ESP_ERR_NO_MEM         thiếu bộ nhớ (netif/task/driver)
 * @return ESP_ERR_INVALID_STATE  gọi sai thứ tự / driver đã init
 * @return ESP_ERR_*              lỗi khác từ ESP-IDF (giữ nguyên)
 */
esp_err_t platform_wifi_start_apsta(const platform_wifi_sta_network_config_t *sta_network,
                                    const platform_wifi_ap_config_t *ap_config,
                                    platform_wifi_event_callback_t callback,
                                    void *context);

/**
 * @brief  Ghi nhớ credentials STA (SSID/password) cho lần kết nối sau.
 *
 *         Chỉ sao chép chuỗi vào storage tĩnh của platform; KHÔNG chạm driver.
 *         Truy cập driver thực tế xảy ra ở platform_wifi_sta_connect()
 *         (trả ESP_ERR_INVALID_STATE nếu provider chưa init).
 *
 * @param  ssid      SSID mạng đích.
 * @param  password  Mật khẩu (WPA2/WPA3/OPEN tùy AP).
 * @return ESP_OK                ghi nhớ thành công
 * @return ESP_ERR_INVALID_ARG   ssid NULL
 */
esp_err_t platform_wifi_sta_set_credentials(const char *ssid, const char *password);

/**
 * @brief  Áp cấu hình mạng STA (DHCP/IP tĩnh) lúc runtime.
 *
 *         dhcp = true  → ESP_OK, KHÔNG chạm DHCP client (không tự chuyển
 *                        static → DHCP; behavior debt của product giữ nguyên).
 *         dhcp = false → dừng DHCP client, set IP/netmask/gateway + DNS chính.
 *         Chuỗi IP được parse đồng bộ (không giữ pointer sau khi trả về).
 *
 * @param  network  Cấu hình mạng STA cần áp.
 * @return ESP_OK                 áp xong (hoặc dhcp = true)
 * @return ESP_ERR_INVALID_ARG    network NULL hoặc IP tĩnh thiếu/không hợp lệ
 * @return ESP_ERR_INVALID_STATE  netif STA chưa tồn tại
 * @return ESP_ERR_*              lỗi khác từ ESP-IDF (giữ nguyên)
 */
esp_err_t platform_wifi_apply_sta_network_config(
    const platform_wifi_sta_network_config_t *network);

/** @brief  Kết nối STA với credentials đã đặt. */
esp_err_t platform_wifi_sta_connect(void);

/** @brief  Ngắt kết nối STA (không xóa credentials). */
esp_err_t platform_wifi_sta_disconnect(void);

/**
 * @brief  Kiểm tra STA đã có IP chưa (trạng thái factual của provider).
 *
 * @return true nếu STA nhận được IP; false nếu chưa/đang kết nối/mất kết nối.
 */
bool platform_wifi_sta_is_connected(void);

/**
 * @brief  Bật SoftAP với cấu hình đã lưu (từ platform_wifi_start_apsta).
 *
 *         Nếu AP đang chạy → ESP_OK không thay đổi. Set mode APSTA nếu cần.
 *
 * @return ESP_OK                AP đang chạy
 * @return ESP_ERR_INVALID_STATE driver chưa init
 * @return ESP_ERR_*             lỗi khác từ ESP-IDF (giữ nguyên)
 */
esp_err_t platform_wifi_ap_start(void);

/**
 * @brief  Tắt SoftAP (chuyển STA-only nếu STA đang dùng).
 *
 *         CHỈ provider operation — policy an toàn (client count > 0, scan
 *         lock) là của caller (product wifi_init).
 *
 * @return ESP_OK                AP đã dừng (hoặc không chạy)
 * @return ESP_ERR_INVALID_STATE driver chưa init
 * @return ESP_ERR_*             lỗi khác từ ESP-IDF (giữ nguyên)
 */
esp_err_t platform_wifi_ap_stop(void);

/** @brief  Số client SoftAP hiện tại (trạng thái factual của provider). */
uint8_t platform_wifi_ap_client_count(void);

/** @brief  SoftAP có đang chạy không (trạng thái factual của provider). */
bool platform_wifi_ap_is_active(void);

/**
 * @brief  Quét Wi-Fi blocking (all channels).
 *
 *         Trước khi quét, provider tự đảm bảo driver có khả năng STA:
 *           - mode đã chứa STA (APSTA/STA) → quét ngay (không đổi mode)
 *           - AP-only                → chuyển APSTA, chờ ~50 ms rồi quét
 *           - NULL (chưa có mode)    → chuyển STA, chờ ~50 ms rồi quét
 *         Không bao giờ chủ động ngắt STA chỉ để quét.
 *
 *         *count là capacity đầu vào của mảng records (số phần tử tối đa),
 *         sau khi trả về là số mạng thực tế ghi vào mảng (bị clamp bởi
 *         capacity). Caller không cần biết số record tối đa của ESP-IDF.
 *
 * @param  config   Cấu hình quét (show_hidden, active window).
 * @param  records  Mảng kết quả (do caller cấp phát; auth_mode được map từ
 *                  authmode ESP-IDF sang platform_wifi_auth_mode_t).
 * @param  count    [in/out] capacity → số kết quả thực tế.
 * @return ESP_OK                quét xong
 * @return ESP_ERR_INVALID_ARG   records/count NULL hoặc *count = 0
 * @return ESP_ERR_INVALID_STATE STA đang hoàn tất kết nối / driver bận
 * @return ESP_ERR_NO_MEM        thiếu bộ nhớ cho bộ đệm quét nội bộ
 * @return ESP_ERR_*             lỗi khác từ ESP-IDF (giữ nguyên)
 */
esp_err_t platform_wifi_scan(const platform_wifi_scan_config_t *config,
                             platform_wifi_scan_record_t *records,
                             uint16_t *count);

/**
 * @brief  Chụp nhanh trạng thái STA hiện tại (SSID/RSSI/IP/gateway).
 *
 * @param  status  [out] Kết quả; khi chưa kết nối, các chuỗi rỗng và rssi
 *                 để mặc định.
 * @return ESP_OK                lấy xong
 * @return ESP_ERR_INVALID_ARG   status NULL
 */
esp_err_t platform_wifi_get_sta_status(platform_wifi_sta_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_WIFI_H */
