/**
 * @file    config_portal.c
 * @brief   Cổng cấu hình HTTP (portal) chạy trên SoftAP của Callbox.
 *
 *          Khi thiết bị bật ở chế độ cấu hình, kỹ thuật viên truy cập
 *          http://192.168.65.204/ (CALLBOX_AP_IP_ADDR) bằng điện thoại để:
 *            - Nhập ID Callbox, mạng WiFi nhà máy (tối đa 5 profile),
 *              IP tĩnh/DHCP, broker MQTT (địa chỉ, cổng, tài khoản).
 *            - Quét mạng WiFi xung quanh (xem RSSI, chế độ bảo mật).
 *            - Theo dõi trạng thái thiết bị: STA, IP, RSSI, MQTT, AP,
 *              trạng thái I/O (nút bấm, tháp đèn, LED, buzzer).
 *            - Quản lý mạng đã nhớ: chọn mạng đang dùng / xóa mạng.
 *
 *          ═══ BẢNG ROUTE HTTP ═══
 *          ┌─────────────────────────────┬──────────────────────────────┐
 *          │ GET  /                      │ Trang cấu hình chính        │
 *          │ POST /login                 │ Đăng nhập (cookie)          │
 *          │ POST /save                  │ Lưu cấu hình + áp dụng WiFi │
 *          │ GET  /logo.jpg              │ Logo công ty (nhúng)        │
 *          │ GET  /api/wifi-scan         │ Quét WiFi (JSON)            │
 *          │ GET  /api/status            │ Trạng thái hệ thống (JSON)  │
 *          │ GET  /api/io-status         │ Trạng thái I/O (JSON)       │
 *          │ GET  /api/config            │ Cấu hình hiện tại (JSON)    │
 *          │ GET/POST /api/wifi-profiles │ DS mạng nhớ / xóa mạng      │
 *          │ POST /api/session/open|ping│ Khóa phiên AP (giữ AP)      │
 *          │ POST /api/session/finish    │ Kết thúc phiên (đóng AP)    │
 *          └─────────────────────────────┴──────────────────────────────┘
 *
 *          Phân quyền: mọi yêu cầu từ mạng AP (192.168.65.0/24) được truy
 *          cập tự do; yêu cầu từ mạng STA (nhà máy) cần cookie đăng nhập
 *          (admin / web_password) hết hạn sau PORTAL_AUTH_TIMEOUT_MS.
 *          Phiên cấu hình giữ AP mở tối đa PORTAL_SESSION_TIMEOUT_MS kể từ
 *          lần tương tác cuối; sau đó AP có thể tự tắt (network_status_task).
 *
 * @note    Trang HTML/CSS/JS được nhúng trực tiếp trong firmware (không phụ
 *          thuộc Internet) — tất cả chuỗi HTML nằm trong các snprintf tĩnh,
 *          chỉ thay đổi khi có nhu cầu giao diện, không sửa khi thêm tính năng.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     config_portal.h — API
 * @see     wifi_init.c — quét WiFi (wifi_scan_lock/unlock), áp cấu hình
 * @see     callbox_config_store.c — lưu/đọc cấu hình
 * @see     io_debug.c — trạng thái I/O cho /api/io-status
 */
#include "config_portal.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "bsp_eth.h"
#include "callbox_io.h"
#include "io_debug.h"
#include "callbox_mqtt.h"
#include "callbox_config_store.h"
#include "platform_wifi.h"
#include "status.h"
#include "time_sync.h"
#include "wifi_init.h"

static const char *TAG = "CONFIG_PORTAL";

/* Con trỏ tới cấu hình toàn cục (do app_main truyền vào config_portal_start) */
static Config_t *s_config = NULL;

/* Handle của HTTP server (một lần duy nhất; gọi lại chỉ trả ESP_OK) */
static httpd_handle_t s_server = NULL;

#define PORTAL_SCAN_MAX_RESULTS 32
#define PORTAL_HTTP_BODY_TIMEOUTS_MAX 1
#define PORTAL_MQTT_PASS_RETAIN_MARKER "__CB_KEEP__"
/* Giữ các mảng lớn (kết quả quét/JSON/HTML) ở tầng tĩnh, KHÔNG nằm trong
 * stack của task HTTP server (ngăn stack overflow). */
static platform_wifi_scan_record_t s_portal_scan_records[PORTAL_SCAN_MAX_RESULTS];
static char s_portal_scan_json[4096];
static char s_io_status_json[768];
/* Trang cấu hình 1 tệp duy nhất nhúng CSS + markup monitor; bộ đệm đủ lớn
 * để stylesheet hoạt động không bao giờ bị cắt cụt. */
static char s_page_html[40960];
/* httpd has one request worker. Keep page-rendering work buffers out of its
 * task stack; a modern portal render otherwise needs several KiB of stack. */
static char s_mqtt_security_script[2000];
static char s_sntp_primary_json[128];
static char s_sntp_fallback_json[128];
static char s_sntp_script[1800];

/* HTTP server has one request worker.  A half-sent POST must not wait
 * forever, otherwise it delays every login and portal API request behind it. */
static esp_err_t portal_receive_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (!req || !body || req->content_len <= 0 || (size_t)req->content_len >= body_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    int received = 0;
    int timeout_count = 0;
    while (received < req->content_len) {
        const int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeout_count <= PORTAL_HTTP_BODY_TIMEOUTS_MAX) {
            continue;
        }
        if (count <= 0) {
            ESP_LOGW(TAG, "HTTP request body aborted after %d/%d byte(s)", received,
                     req->content_len);
            return count == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        received += count;
        timeout_count = 0;
    }
    body[received] = '\0';
    return ESP_OK;
}

/* Hạn phiên cấu hình (ms): nếu không tương tác quá lâu thì AP được phép tắt */
static volatile TickType_t s_session_deadline;
#define PORTAL_SESSION_TIMEOUT_MS 30000
/* Hạn cookie đăng nhập STA: 30 phút kể từ khi đăng nhập */
#define PORTAL_AUTH_TIMEOUT_MS (30U * 60U * 1000U)
#define PORTAL_STA_USERNAME "admin"
#define PORTAL_AUTH_COOKIE "cb_auth"
/* Token ngẫu nhiên 32 ký tự xác thực cookie (sinh tại mỗi lần đăng nhập) */
static char s_auth_token[33];
static volatile TickType_t s_auth_deadline;

/* Logo công ty nhúng trong firmware, phục vụ như một asset tĩnh nhỏ. */
extern const uint8_t _binary_company_logo_transparent_png_start[];
extern const uint8_t _binary_company_logo_transparent_png_end[];

/* Kiểm tra chuỗi có phải địa chỉ IPv4 hợp lệ (dùng khi cấu hình IP tĩnh) */
static bool valid_ipv4(const char *value)
{
    esp_ip4_addr_t address;
    return value && value[0] && esp_netif_str_to_ip4(value, &address) == ESP_OK;
}

/* Kiểm tra ID Callbox chỉ chứa chữ số (kế hoạch đặt tên AUBOT-Callbox-<số>) */
static bool valid_numeric_callbox_id(const char *value)
{
    if (!value || !value[0]) return false;
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

/* Đầu vào host NTP cố tình giới hạn: chỉ IPv4 hoặc hostname DNS. */
static bool valid_sntp_host(const char *value)
{
    if (!value || !value[0]) return false;
    for (const char *p = value; *p; ++p) {
        const bool valid = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                           (*p >= '0' && *p <= '9') || *p == '.' || *p == '-';
        if (!valid) return false;
    }
    return true;
}

/* Tạo tên thiết bị hiển thị: CALLBOX_DEVICE_NAME_PREFIX + id (vd AUBOT-Callbox-01) */
static void format_device_name(const char *id, char *out, size_t out_size)
{
    snprintf(out, out_size, CALLBOX_DEVICE_NAME_PREFIX "%s", id ? id : "");
}

/* Wi-Fi là kết nối trực tiếp đang hoạt động. Lưu ID thiết bị hoặc cài đặt
 * MQTT không được cấu hình lại STA, vì điều đó làm gián đoạn liên kết lành. */
static bool wifi_runtime_config_changed(const Config_t *before, const Config_t *after)
{
    if (!before || !after) return true;
    if (before->wifi_dhcp != after->wifi_dhcp ||
        strcmp(before->wifi_ssid, after->wifi_ssid) != 0 ||
        strcmp(before->wifi_pass, after->wifi_pass) != 0 ||
        strcmp(before->wifi_ip, after->wifi_ip) != 0 ||
        strcmp(before->wifi_netmask, after->wifi_netmask) != 0 ||
        strcmp(before->wifi_gateway, after->wifi_gateway) != 0 ||
        strcmp(before->wifi_dns, after->wifi_dns) != 0 ||
        before->wifi_profile_count != after->wifi_profile_count) {
        return true;
    }

    const uint8_t count = before->wifi_profile_count > MAX_WIFI_PROFILES
                              ? MAX_WIFI_PROFILES : before->wifi_profile_count;
    for (uint8_t i = 0; i < count; ++i) {
        if (strcmp(before->wifi_profiles[i].ssid, after->wifi_profiles[i].ssid) != 0 ||
            strcmp(before->wifi_profiles[i].password, after->wifi_profiles[i].password) != 0) {
            return true;
        }
    }
    return false;
}

/* MQTT có thể được khởi động lại độc lập với STA. Phần so sánh cố tình bao
 * gồm ID logic vì nó cũng là client ID MQTT và gốc topic được sinh ra. */
static bool mqtt_runtime_config_changed(const Config_t *before, const Config_t *after)
{
    if (!before || !after) return true;
    return before->mqtt_port != after->mqtt_port ||
           before->mqtt_transport != after->mqtt_transport ||
           strcmp(before->mqtt_broker, after->mqtt_broker) != 0 ||
           strcmp(before->mqtt_user, after->mqtt_user) != 0 ||
           strcmp(before->mqtt_pass, after->mqtt_pass) != 0 ||
           strcmp(before->callbox_id, after->callbox_id) != 0;
}

static void play_config_saved_tone(void)
{
    /* Lưu thành công là một hành động ứng dụng của Callbox, nên xác nhận qua
     * buzzer tower trên DO1. GPIO46 được giữ riêng cho thông báo kết nối/ngắt
     * mạng STA trong network_status_task. */
    status_request_feedback(OUTPUT_FEEDBACK_CONFIG_SAVED);
}

/* Giữ markup fallback/kế thừa và trang tiếng Việt đang hoạt động trên một URL AP.
 * Thay thế mọi chuỗi IP cũ 192.168.4.1 trong HTML bằng CALLBOX_AP_IP_ADDR
 * (192.168.65.204) — giúp 1 trang phục vụ cả bản mới lẫn markup kế thừa. */
static void replace_legacy_ap_ip(void)
{
    static const char old_ip[] = "192.168.4.1";
    const size_t old_len = sizeof(old_ip) - 1U;
    const size_t new_len = sizeof(CALLBOX_AP_IP_ADDR) - 1U;
    char *cursor = s_page_html;

    while ((cursor = strstr(cursor, old_ip)) != NULL) {
        const size_t offset = (size_t)(cursor - s_page_html);
        const size_t page_len = strlen(s_page_html);
        if (new_len > old_len && page_len + (new_len - old_len) + 1U >= sizeof(s_page_html)) {
            break;
        }
        memmove(cursor + new_len, cursor + old_len,
                page_len - offset - old_len + 1U);
        memcpy(cursor, CALLBOX_AP_IP_ADDR, new_len);
        cursor += new_len;
    }
}

/* Chèn đoạn markup (CSS/JS) vào trước vị trí needle trong trang HTML;
 * trả false nếu không tìm thấy needle hoặc bộ đệm s_page_html không đủ chỗ. */
static bool page_insert_before(const char *needle, const char *markup)
{
    char *position = strstr(s_page_html, needle);
    if (!position || !markup) return false;
    const size_t page_len = strlen(s_page_html);
    const size_t markup_len = strlen(markup);
    /* Kiểm tra tràn: cần đủ chỗ cho chính markup + ký tự kết thúc NULL */
    if (page_len + markup_len + 1U >= sizeof(s_page_html)) return false;
    const size_t offset = (size_t)(position - s_page_html);
    /* Dịch phần còn lại của trang sang phải rồi chèn markup vào */
    memmove(position + markup_len, position, page_len - offset + 1U);
    memcpy(position, markup, markup_len);
    return true;
}

/* Trang cấu hình được thu gọn (minify) và nén gzip trước khi lưu vào flash. */
static const uint8_t s_page_gzip[] __attribute__((unused)) = {
    "\x1F\x8B\x08\x00\x00\x00\x00\x00\x02\x0A\xAD\x57\xDD\x8E\xDB\xB8\x15\xBE\x2F\xD0\x77\xE0\x3A\x29\x64\x77\xC7\xB2\x24\x8F\x1D\x8F\x2C\x69\xD1\x4C\xB6\xBB\x03\xEC\x6E\xA7\x98\x14\xBD\x28\x8A\x80\x12\x69\x8B\xB5\x4C\x2A\x14\x35\xB6\xD7\xD1\x73\xF5\xA2\x17\x05\xFA\x42\x7D\x85\x1E\x92\x92\x2D\x7B\x92\x76\x2F\x8A\x20\x18\x91\x3C\xFF\xE7\x3B\x1F\xE9\x7F\xFF\xE3\x9F\xD1\x57\x44\x64\xEA\x50\x52\x94\xAB\x6D\x91\x44\x5B\xAA\x30\xCA\x72\x2C\x2B\xAA\xE2\x5A\xAD\xC6\x8B\x76\x8F\xE3\x2D\x8D\x9F\x19\xDD\x95\x42\x2A\x94\x09\xAE\x28\x57\xF1\x60\xC7\x88\xCA\x63\x42\x9F\x59\x46\xC7\x66\x71\xC3\x38\x53\x0C\x17\xE3\x2A\xC3\x05\x8D\xFD\x41\x12\x29\xA6\x0A\x9A\xDC\xE3\xA2\x48\xC5\x1E\x81\xE5\xBA\x8C\x26\x76\x33\xAA\xD4\x01\xFE\x84\x52\x08\x75\xCC\x44\x21\x24\xE8\xE5\x74\x4B\x43\x82\xE5\xA6\xF9\xED\x11\x34\xC6\x15\xFB\x99\xF1\x75\x98\x0A\x49\xA8\x1C\xC3\x4E\x93\x0A\x72\x38\x6E\xB1\x5C\x33\x1E\x7A\xCB\x12\x13\xA2\x05\x02\xAF\xDC\x2F\x57\x10\x5A\xE8\xCF\x4A\x70\x74\xA8\x14\xDD\x8E\x6B\x76\x53\x61\x5E\x8D\x2B\x2A\xD9\x6A\x99\xE2\x6C\xB3\x96\xA2\xE6\x24\x7C\xE5\xAD\xFC\x37\x01\x5E\x1A\xB7\xE1\x2B\x1A\xD0\xC5\xCA\x6B\xB6\x98\x71\x30\xBD\xB7\xD9\x84\x73\x63\xB4\x75\x85\x6B\x25\x2E\x2C\xF8\x34\xB8\x9B\xA6\x4B\x1B\x59\xE8\x6B\xA7\xA2\x60\x04\xBD\x9A\x4E\x6F\xFD\xD9\xAC\x3D\x18\x4B\x4C\x58\x5D\x85\xFE\x1C\x4C\x9D\x82\xBD\x85\x85\xC9\x2E\xC7\x44\xEC\x42\x0F\xF9\x01\xE8\xDF\x82\x3B\xF4\xCA\xF3\xBC\x79\x93\xFB\xA7\x14\x91\x87\xE6\x6D\x6E\xBA\x1A\x34\x0C\x20\xC1\xA6\x3C\xB6\xA1\xDF\xDD\xE2\x69\xBA\xE8\xA2\x04\x49\x90\xD7\x71\x37\x79\x70\x3C\xEB\x18\xF7\xAD\x4C\x10\x18\xA1\x05\xEC\xB4\x36\xDE\x10\x32\x5D\x65\x4D\x81\x53\x5A\x1C\x09\xAB\xCA\x02\x1F\xC2\xB4\x10\xD9\xA6\xD3\xF1\xAD\xCE\xEC\xAC\x93\xA5\x64\x46\xFD\x86\xF1\xB2\x56\x37\x15\x2D\x68\xA6\x8E\xB6\x6C\xBE\xE7\xFD\xE6\x94\xAA\xEF\x9B\x54\xAF\x6B\x74\xFB\x66\x36\x9B\xDF\x5D\xD5\x48\x47\xF4\xE5\x1E\xAD\x16\x2B\xBC\xCA\x6C\x8F\x19\xCF\xA1\xA3\xAA\x49\x6B\xA5\x04\x3F\xF6\x9D\x21\xDD\xFF\xCE\xA3\xF7\x3F\x3C\xF8\x5E\x7A\xB7\xF0\x3B\x0F\xDE\x2C\xA0\xFE\xDC\x56\x7A\x47\xD9\x3A\x57\xE1\x1B\xCF\x5B\x66\xB5\xAC\xE0\xB8\x14\x0C\x80\x2F\x5B\x9F\x6E\x45\x61\x10\x00\xA8\x87\x63\xDF\xE0\x74\x91\x92\xD5\xE2\x64\x70\x11\xAC\x6E\xEF\x5A\x8D\x10\x0A\x8B\xD3\x82\x92\xA3\x28\x71\xC6\xD4\x21\x74\x67\x8D\x2B\xC5\xEE\x54\xF1\x55\x41\xF7\xCB\x35\x2E\x75\x98\xE6\x04\x99\xEA\x1E\xF5\x7E\xE8\xDB\x9D\x36\xE3\x5D\xCE\x14\x1D\x57\x60\x88\x86\x5C\xEC\x24\x2E\x1B\x37\x87\xF8\xFA\x2D\x0F\xCE\xCD\xB2\x20\x69\xDC\x0A\x3F\xD3\x7E\x97\x6C\x77\xC7\x4A\x94\xE7\x01\x3A\x03\xA6\x89\x26\x76\x44\x23\x3D\x18\x49\x94\xFB\xD7\x83\x0C\x3B\x51\x99\xFC\x81\x53\xB4\x62\x72\xBB\xC3\x12\x3E\x84\x44\xF4\x99\xCA\x03\xAA\x81\x0C\x5C\xF4\x44\x15\x52\x39\x45\x0F\xEF\x6E\xD0\x9F\xD9\xEF\x19\xC2\x9C\xA0\x1F\xFF\xF8\xFE\xBD\xB6\xA1\xA0\x6B\x15\x02\xD4\x89\x9D\x1B\x4D\xCA\x24\x02\xED\x2D\x02\xD2\xC9\x05\x89\x4B\x51\x29\x84\x33\xC5\x04\x8F\x27\x3A\x72\x88\x20\x48\x1E\x08\x90\x0F\x54\x0F\x9C\x07\x49\x64\x20\x7B\x8A\xEA\xE1\x5D\x34\xB1\x3B\x91\xA9\x1C\x62\x24\x06\xB4\x19\xFE\xCA\xAC\xCC\x07\x58\xC3\x84\x17\x94\xAF\x81\xBC\xFC\x19\x92\xF4\x63\xCD\x24\x25\x08\x5A\x90\xD1\x5C\x14\x00\x97\x38\x4B\x3D\xDF\x78\xD3\x11\xF7\x3D\x3D\x3D\xF5\x7C\x10\xF6\x8C\xB2\x02\x57\x55\x0C\x9D\xE9\xB9\xAC\xAA\xCE\xE9\x8E\xAD\xD8\x07\xB3\x3C\xFB\x9C\x06\x27\x9F\x49\x64\xDB\x89\x34\x0B\xC7\xED\xB7\xB5\x78\x82\x17\x12\x3C\x2B\x58\xB6\x89\x81\x53\xF9\x70\x94\x3C\xC1\x9F\x68\x62\x65\x93\x68\x02\x31\x00\x91\x9A\xF1\xD3\xAE\x39\x54\x1B\x14\x72\xCC\xD7\x34\x1E\x68\xCF\xEE\x33\x2E\x6A\x1A\xAB\x9C\x55\xF6\x13\x48\x59\x94\xBA\xA8\xC8\x9E\x0C\x06\xC9\x93\xD5\xC7\x48\xFB\xE0\x50\x0B\x30\xB3\x13\x72\x13\x4D\xAC\x24\xF8\xB1\x2E\xBA\x32\x3C\x42\x88\x20\x40\xAE\xCA\x7D\xCE\xB9\x04\x01\x9B\x55\xD9\x8A\xF6\x2A\x30\x9F\x5E\x14\x7B\xF0\x03\x85\xE6\xA2\xB4\xC0\x7C\x83\x94\x40\x1B\x4A\x4B\x04\x43\x27\xA1\xD1\x03\xDB\xF3\x47\xB4\x15\x84\xDA\x46\x9C\x93\x25\x79\x56\xF6\x7C\x9A\xE5\x29\x79\xAD\x00\xE5\xBA\xCC\xD5\x4F\xDE\x7D\x7F\xFF\x88\xC6\x88\x1C\x40\x8F\x65\xE8\xE1\xF1\x9C\xE3\x85\xA4\x97\x3C\x29\xAC\x40\x62\x0C\xD0\xDE\x43\x49\xFA\x92\xA7\x6A\x68\x04\x68\x8C\x95\x66\x26\xF4\xAC\xC4\x83\x6E\x9A\xB9\xE0\xBA\xD6\xB6\x40\x90\x01\x70\x94\xA4\x55\xF5\x19\x84\xF6\x93\x80\xC5\x05\x3C\x2F\x0A\xE5\xDF\x05\xAE\x3F\x5F\xB8\xBE\x3B\xF3\x4E\xA6\x7F\xA2\x6A\x8B\xAB\xCD\x4B\xBB\x7A\xB7\x67\x99\x5B\xB9\xFF\x62\x3E\x98\xCD\xDC\xEE\xFF\xD9\xFE\x77\x58\xD1\x1D\x3E\xBC\xB4\xBF\xDE\xF5\xAC\xAF\xAD\xD4\x2F\x0A\xDE\x3F\xD9\x7E\xF7\xD3\xD3\x4B\xBB\x84\x57\xFD\xAE\xC2\xEA\xCB\x46\x17\xAE\xF9\x37\xE8\x26\x01\x00\xA2\xB9\xA5\x3F\xB2\x6F\xA5\xD8\x50\x89\x26\xA6\x83\xD7\xBE\x52\x7B\x68\xDC\x6D\x3F\x2A\xF5\xA1\xDD\xB8\x00\xEB\x79\x5C\x5B\xF4\xC3\x33\xE8\xA5\x29\xF3\x38\x3A\x1B\x32\x4B\x33\x01\xBC\xDE\xA6\xDA\x24\xE3\xB1\xAF\x0D\xC7\xF3\xD9\x6C\x3A\x7B\x61\xF5\x4F\xF0\x48\x79\x69\xB5\xAE\x2E\xC2\x33\xCB\x1E\x97\xF8\xBF\x64\x22\x6D\x3C\xFF\xBF\x89\xBC\x24\x2A\x4D\xCC\x4F\x5A\x5C\x13\xBB\xA4\x29\xBC\xE6\xCE\xFC\x54\xB6\x52\xFA\x6E\x4A\xEE\x05\x5F\xB1\x75\x2D\xB1\x99\xB3\xDF\x3D\xA2\x2E\x92\x10\xB5\xEC\xEC\x07\x53\xF4\xAF\xBF\x23\x51\x52\x0E\x8F\x52\x55\x86\x93\x49\x07\x9B\x5B\xD7\x37\x37\xC4\x44\x5F\x11\xC0\x02\x99\x64\xA5\x4A\x80\x24\xE1\x92\x78\x1D\xEF\xE3\x04\x1E\xB3\xF5\x16\x02\x74\xD7\x54\x7D\x5B\x50\xFD\xF9\xF6\xF0\x40\x86\xFB\xD1\x12\x57\x07\x9E\xA1\x55\xCD\xCD\x65\x82\x0A\x81\xC9\x70\x74\x54\x70\x77\x17\xC0\x97\x59\x8C\x77\x98\x29\xB4\xA2\x2A\xCB\x87\xCE\x04\x97\x6C\x92\x99\x50\x9D\x91\x0B\x57\x17\x1F\xCA\x38\x91\xEE\xDF\x2A\x01\xFC\x3B\x5A\xBE\x1E\x3A\x8C\xC0\x89\xE5\x89\xCC\x3D\x5F\x2C\x9F\x3E\x39\x8E\x3E\xD6\xB4\xDB\x13\x38\x5D\x02\xDD\xB9\xC5\x58\x4F\xA2\x87\xBC\x4E\x46\xC3\xE7\x5A\x42\xEF\x7D\xFA\xE4\x2F\x16\xB7\x5A\x42\x43\xE1\x5A\x42\xEF\x75\x16\x34\x1D\x5E\x47\xA1\xF7\xBE\x71\x7C\x27\x74\x3C\x23\xC3\x5E\x48\xB0\xB2\xD3\xD7\x64\x71\x7D\xDA\x72\x48\x27\xB2\xDE\x5D\x0B\xB4\x34\x70\x8A\x81\x57\x2F\x42\xE0\x95\x39\xB5\xFC\xDC\x64\x58\x17\x9D\x8E\x8E\x4D\x73\xEA\x8F\x3D\x32\xBD\x21\xF1\x75\x26\x71\x0C\xF1\xDB\xD8\xA1\xEA\xB0\x6D\x68\xD7\x6D\x59\x37\x26\xDF\x38\x9A\x78\x21\x43\xF3\x7E\x75\x96\x7F\xD1\x49\xDE\xD8\x6C\x6E\x74\xC4\x37\x26\xAA\xBF\xBA\x80\xA3\x6F\x31\xF8\x06\xE8\xBC\x06\x90\xB8\xDD\x3C\xC6\x5F\x91\x51\x73\x85\x18\x7B\xF7\x9A\x88\xD2\xF8\x04\xB4\x8F\x35\x3C\x73\xEC\xDD\x29\xE4\xD0\x39\x3F\x09\x9D\xD1\x32\x75\xBB\xE7\x5E\xEC\xC3\x42\xD1\xBD\xBA\x6F\x7F\x38\x39\xAE\xEB\x3A\xCB\x0E\x7D\xF8\x33\xE8\xD3\x85\xD2\xBF\xA1\xF8\xE7\x00\x78\xC3\x75\x4D\xA0\x13\xE0\x85\xBB\x0C\xEE\x6B\xF9\xFD\xFB\x1F\x7F\x88\x9D\x2F\x5F\xEB\xD7\xD7\xB9\xB3\xC4\xFD\xF4\x4D\x1C\xE2\x9C\x57\x26\x29\xB4\xB1\x9D\xA1\xA1\x63\x95\xC0\x9B\x68\x5B\xB0\x77\x35\x9A\x61\xD9\xCF\xCA\x6E\x7E\xED\xA0\xA1\xF3\xF5\xDE\x95\xB0\x80\x6F\xF2\x76\x3B\x72\x20\x4A\x5C\xC2\x44\x93\xFB\x9C\x15\x64\x28\x46\x4D\xAF\xED\xF0\x3B\x51\x82\x0F\xF3\x32\xD4\x19\xA3\x15\x66\x50\x34\x67\xD4\xF4\x2A\xE8\x5D\x57\x50\x3F\x82\x9C\xC6\xCE\x31\xDC\xC6\x96\x0B\xA2\x89\x79\xA6\xFE\xFA\x57\xFF\x01\x75\xD6\x56\xA4\xDC\x0E\x00\x00",
};

/* Thoát ký tự đặc biệt JSON: dấu nháy kép " và backslash \ → \", \\
 * để nội dung cấu hình (SSID, broker…) không phá vỡ chuỗi JSON trả về. */
static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t pos = 0;
    if (dst_size == 0) return;
    for (; src && *src && pos + 1 < dst_size; src++) {
        if ((*src == '"' || *src == '\\') && pos + 2 < dst_size) {
            dst[pos++] = '\\';
        }
        dst[pos++] = *src;
    }
    dst[pos] = '\0';
}

/* Chuyển 1 ký tự hex ('0'-'9','a'-'f','A'-'F') sang giá trị 0..15; -1 nếu không hợp lệ */
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Giải mã URL-encoding tại chỗ (không cấp bộ đệm mới):
 *   '+' → khoảng trắng; '%XX' → ký tự nhị phân tương ứng; còn lại giữ nguyên. */
static void url_decode(char *value)
{
    char *src = value;
    char *dst = value;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && hex_value(src[1]) >= 0 && hex_value(src[2]) >= 0) {
            *dst++ = (char)((hex_value(src[1]) << 4) | hex_value(src[2]));
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* Lấy giá trị của 1 trường trong thân form x-www-form-urlencoded.
 * Tách chuỗi theo '&', so sánh key, giải mã URL và chép sang out. */
static bool form_value(const char *body, const char *key, char *out, size_t out_size)
{
    if (!body || !key || !out || out_size == 0) return false;

    /* Sao chép thân form vào bộ đệm địa phương để có thể sửa (strtok_r) */
    char copy[1024];
    strncpy(copy, body, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *save = NULL;
    for (char *item = strtok_r(copy, "&", &save); item; item = strtok_r(NULL, "&", &save)) {
        /* Mỗi item có dạng key[=value]; tách dấu '=' đầu tiên */
        char *equals = strchr(item, '=');
        if (!equals) continue;
        *equals = '\0';
        /* Không khớp key → bỏ qua */
        if (strcmp(item, key) != 0) continue;
        /* Khớp: giải mã phần giá trị rồi ghi vào out */
        url_decode(equals + 1);
        strncpy(out, equals + 1, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }
    return false;
}

/* Thoát ký tự nhạy cảm HTML trước khi nhúng vào trang:
 *   & → &amp;   < → &lt;   > → &gt;   " → &quot;   ' → &#39;
 * Chống chèn mã (XSS) khi hiển thị cấu hình người dùng (SSID, broker…). */
static void html_escape(const char *src, char *dst, size_t dst_size)
{
    size_t pos = 0;
    if (dst_size == 0) return;
    for (; src && *src && pos + 1 < dst_size; src++) {
        const char *replacement = NULL;
        switch (*src) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"': replacement = "&quot;"; break;
            case '\'': replacement = "&#39;"; break;
            default: dst[pos++] = *src; continue;
        }
        size_t length = strlen(replacement);
        if (pos + length >= dst_size) break;
        memcpy(&dst[pos], replacement, length);
        pos += length;
    }
    dst[pos] = '\0';
}

/* Lấy địa chỉ IPv4 của peer (trình duyệt) từ socket của request HTTP.
 * Dùng để nhận diện khách từ mạng AP cấu hình khi getsockname() không đủ tin. */
static bool request_peer_ipv4(httpd_req_t *req, uint32_t *address)
{
    if (!req || !address) return false;

    const int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in peer = { 0 };
    socklen_t peer_len = sizeof(peer);
    if (sockfd < 0 || getpeername(sockfd, (struct sockaddr *)&peer, &peer_len) != 0 ||
        peer.sin_family != AF_INET) {
        return false;
    }
    *address = peer.sin_addr.s_addr;
    return true;
}

/* Xác định request đến từ mạng AP cấu hình hay không:
 *   - Nếu socket cục bộ gắn đúng IP của AP (CALLBOX_AP_IP_ADDR) → AP.
 *   - Fallback: peer IP thuộc dải 192.168.65.0/24 (C0A84900) — dùng cho
 *     stack trả về INADDR_ANY từ getsockname(). */
static bool request_from_local_ap(httpd_req_t *req)
{
    const int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in local = { 0 };
    socklen_t local_len = sizeof(local);
    if (sockfd >= 0 && getsockname(sockfd, (struct sockaddr *)&local, &local_len) == 0 &&
        local.sin_family == AF_INET && local.sin_addr.s_addr == inet_addr(CALLBOX_AP_IP_ADDR)) {
        return true;
    }

    /* Dự phòng cho các stack báo INADDR_ANY từ getsockname(). */
    uint32_t peer_ip = 0;
    return request_peer_ipv4(req, &peer_ip) &&
           (ntohl(peer_ip) & 0xFFFFFF00UL) == 0xC0A84100UL;
}

/* Kiểm tra cookie đăng nhập portal còn hiệu lực:
 *   - Token chưa hết hạn (s_auth_deadline) và đã được sinh.
 *   - Cookie HTTP có chứa chuỗi đúng 'cb_auth=<token>'. */
static bool request_has_sta_login(httpd_req_t *req)
{
    if (s_auth_deadline == 0 ||
        (int32_t)(s_auth_deadline - xTaskGetTickCount()) <= 0 ||
        !s_auth_token[0]) {
        return false;
    }

    const size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (cookie_len == 0 || cookie_len >= 160) return false;
    char cookie[160];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }

    /* Tìm 'cb_auth=' trong chuỗi Cookie, đảm bảo là từ riêng (không phải đuôi khác) */
    const char *value = strstr(cookie, PORTAL_AUTH_COOKIE "=");
    if (!value || (value != cookie && value[-1] != ';' && value[-1] != ' ')) return false;
    value += sizeof(PORTAL_AUTH_COOKIE);
    const size_t value_len = strcspn(value, ";");
    /* So sánh chính xác độ dài và nội dung token */
    return value_len == strlen(s_auth_token) &&
           strncmp(value, s_auth_token, value_len) == 0;
}

/* Mọi đường vào portal, gồm cả AP cứu hộ và STA, đều cần cookie hợp lệ. */
static bool request_is_authorized(httpd_req_t *req)
{
    return request_has_sta_login(req);
}

/* Yêu cầu quyền truy cập portal cho 1 handler; nếu không có quyền thì trả
 * 401 tức thì (các endpoint API bắt đầu bằng lời gọi này). */
static bool require_portal_access(httpd_req_t *req)
{
    if (request_is_authorized(req)) return true;
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Login required");
    return false;
}

/* Gửi trang đăng nhập (HTML nhúng) cho người truy cập từ mạng STA.
 * Tham số error: chuỗi thông báo lỗi (đã là markup HTML) hiển thị trên trang. */
static esp_err_t send_login_page(httpd_req_t *req, const char *error)
{
    const char *message = error ? error : "";
    char page[4096];
    snprintf(page, sizeof(page),
             "<!doctype html><html lang='vi'><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>&#x110;&#x103;ng nh&#x1EAD;p Callbox</title><style>"
             "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;"
             "padding:20px;background:#0d1525;color:#f8fafc;font:16px/1.5 system-ui,-apple-system,Segoe UI,Arial,sans-serif}"
             "main{width:min(420px,100%%);background:#172236;border:1px solid #3b4b64;border-radius:12px;padding:30px;box-shadow:0 18px 45px rgba(2,6,23,.3)}"
             "img{display:block;width:180px;max-height:42px;object-fit:contain;margin-bottom:25px}h1{font-size:23px;margin:0 0 6px}p{margin:0 0 22px;color:#a9b7ca}label{display:block;margin:14px 0 6px;color:#a9b7ca;font-size:13px;font-weight:650}input{width:100%%;min-height:46px;padding:9px 12px;border:1px solid #3b4b64;border-radius:8px;background:#111a2c;color:#f8fafc;font:inherit}input:focus-visible{outline:3px solid rgba(52,211,153,.18);border-color:#34d399}button{width:100%%;min-height:50px;margin-top:22px;border:1px solid #047857;border-radius:8px;background:#047857;color:#ecfdf5;font:650 15px inherit;cursor:pointer}button:hover{background:#065f46}.error{margin:0 0 12px;padding:9px 11px;border:1px solid #f87171;border-radius:8px;background:rgba(248,113,113,.1);color:#fecaca;font-size:14px}.hint{margin-top:18px;margin-bottom:0;font-size:13px;color:#8190a6}</style></head><body><main>"
             "<img src='/logo.jpg?v=4' alt='AUBOT'><h1>&#x110;&#x103;ng nh&#x1EAD;p c&#x1EA5;u h&#xEC;nh</h1><p>Vui l&#xF2;ng x&#xE1;c th&#x1EF1;c tr&#x1B0;&#x1EDB;c khi c&#x1EA5;u h&#xEC;nh thi&#x1EBF;t b&#x1ECB;.</p>%s"
             "<form method='post' action='/login'><label for='username'>T&#xE0;i kho&#x1EA3;n</label><input id='username' name='username' autocomplete='username' required autofocus>"
             "<label for='password'>M&#x1EAD;t kh&#x1EA9;u</label><input id='password' name='password' type='password' autocomplete='current-password' required>"
             "<button type='submit'>&#x110;&#x103;ng nh&#x1EAD;p</button></form></main></body></html>",
             message);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

/* GET /api/wifi-scan — quét WiFi và trả danh sách mạng dạng JSON.
 * Sử dụng wifi_scan_lock/unlock để đồng bộ với task chọn mạng nền.
 * Provider mechanics (mode scan-capable, raw scan) nằm ở platform_wifi. */
static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    /* Never hold the sole HTTP worker waiting for a background scan. */
    if (!wifi_scan_lock(0)) {
        ESP_LOGW(TAG, "WiFi scan busy; portal remains responsive");
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "WiFi scan busy; try again shortly");
        return ESP_OK;
    }

    const platform_wifi_scan_config_t scan_config = {
        .show_hidden = true,
        .active_min_ms = 100,
        .active_max_ms = 300,
    };

    uint16_t count = PORTAL_SCAN_MAX_RESULTS;
    memset(s_portal_scan_records, 0, sizeof(s_portal_scan_records));
    const esp_err_t err = platform_wifi_scan(&scan_config, s_portal_scan_records, &count);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi scan completed: %u network(s)", (unsigned)count);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi scan failed");
        wifi_scan_unlock();
        return ESP_OK;
    }

    const uint16_t max_results = count > PORTAL_SCAN_MAX_RESULTS ? PORTAL_SCAN_MAX_RESULTS : count;
    char *json = s_portal_scan_json;
    size_t pos = 0;
    /* Dựng mảng JSON: [{"ssid":...,"rssi":...,"auth":...}, ...]
     * auth: giá trị platform (giữ nguyên mã số legacy portal — xem
     * platform_wifi_auth_mode_t). */
    pos += snprintf(json + pos, sizeof(s_portal_scan_json) - pos, "[");
    for (uint16_t i = 0; i < max_results && pos + 32 < sizeof(s_portal_scan_json); i++) {
        char ssid[65];
        /* Thoát SSID trước khi nhúng vào JSON */
        json_escape((const char *)s_portal_scan_records[i].ssid, ssid, sizeof(ssid));
        if (ssid[0] == '\0') continue;
        /* Dấu phẩy phân cách giữa các phần tử (kiểm tra pos > 1 = đã có [) */
        pos += snprintf(json + pos, sizeof(s_portal_scan_json) - pos,
                        "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
                        (pos > 1) ? "," : "", ssid,
                        s_portal_scan_records[i].rssi,
                        (int)s_portal_scan_records[i].auth_mode);
    }
    snprintf(json + pos, sizeof(s_portal_scan_json) - pos, "]");

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    wifi_scan_unlock();
    return send_err;
}

/* GET /logo.jpg — phục vụ logo công ty nhúng trong firmware (ảnh PNG tĩnh).
 * Địa chỉ biểu tượng được khai báo ngoài (linker đặt vào flash qua .bin). */
static esp_err_t logo_handler(httpd_req_t *req)
{
    /* Kích thước = hiệu hai con trỏ biên (start/end) do linker tạo */
    const size_t logo_size = (size_t)(_binary_company_logo_transparent_png_end -
                                      _binary_company_logo_transparent_png_start);
    httpd_resp_set_type(req, "image/png");
    /* Cache 1 ngày để trình duyệt không tải lại logo mỗi lần mở trang */
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)_binary_company_logo_transparent_png_start,
                           logo_size);
}

/* (Không dùng) Trang cấu hình phiên bản 1 — giữ lại để tham khảo/hồi phục.
 * Hiển thị form đơn giản: ID, WiFi, MQTT với POST tới /save. */
static esp_err_t __attribute__((unused)) send_config_page(httpd_req_t *req)
{
    char callbox_id[64], broker[128], ssid[128];
    html_escape(s_config->callbox_id, callbox_id, sizeof(callbox_id));
    html_escape(s_config->mqtt_broker, broker, sizeof(broker));
    html_escape(s_config->wifi_ssid, ssid, sizeof(ssid));

    char page[4096];
    snprintf(page, sizeof(page),
             "<!doctype html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Callbox config</title>"
             "<style>body{font-family:Arial;max-width:600px;margin:24px auto;padding:0 16px}"
             "input{width:100%%;padding:10px;margin:5px 0 14px;box-sizing:border-box}"
             "button{padding:12px 20px;background:#087f5b;color:white;border:0;border-radius:5px}"
             "small{color:#666}</style></head><body>"
             "<h2>Callbox configuration</h2>"
             "<p>Local AP: <b>CALLBOX-%s</b>. LÃ†Â°u xong thiÃ¡ÂºÂ¿t bÃ¡Â»â€¹ sÃ¡ÂºÂ½ khÃ¡Â»Å¸i Ã„â€˜Ã¡Â»â„¢ng lÃ¡ÂºÂ¡i.</p>"
             "<form method='post' action='/save'>"
             "<label>Callbox ID</label><input name='callbox_id' value='%s' maxlength='15' required>"
             "<h3>WiFi mÃ¡ÂºÂ¡ng</h3>"
             "<label>SSID</label><input name='wifi_ssid' value='%s' maxlength='32'>"
             "<label>MÃ¡ÂºÂ­t khÃ¡ÂºÂ©u</label><input type='password' name='wifi_pass' maxlength='63' placeholder='Ã„ÂÃ¡Â»Æ’ trÃ¡Â»â€˜ng Ã„â€˜Ã¡Â»Æ’ giÃ¡Â»Â¯ nguyÃƒÂªn'>"
             "<h3>MQTT</h3>"
             "<label>Broker/IP</label><input name='mqtt_broker' value='%s' maxlength='63' required>"
             "<label>Port</label><input type='number' name='mqtt_port' value='%u' min='1' max='65535' required>"
             "<label>User</label><input name='mqtt_user' value='%s' maxlength='31'>"
             "<label>Password</label><input type='password' name='mqtt_pass' maxlength='63' placeholder='Ã„ÂÃ¡Â»Æ’ trÃ¡Â»â€˜ng Ã„â€˜Ã¡Â»Æ’ giÃ¡Â»Â¯ nguyÃƒÂªn'>"
             "<button type='submit'>LÃ†Â°u vÃƒÂ  khÃ¡Â»Å¸i Ã„â€˜Ã¡Â»â„¢ng lÃ¡ÂºÂ¡i</button></form>"
             "<p><small>M&#x1EAD;t kh&#x1EA9;u AP tr&#x00F9;ng t&#x00EA;n WiFi AP.</small></p></body></html>",
             callbox_id, callbox_id, ssid, broker, (unsigned)s_config->mqtt_port,
             s_config->mqtt_user);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

/* (Không dùng) Trang cấu hình phiên bản 2 — có nút "Scan nearby WiFi".
 * Tham chiếu: URL AP vẫn ghi 192.168.4.1 (thời điểm phát triển ban đầu). */
static esp_err_t __attribute__((unused)) send_config_page_v2(httpd_req_t *req)
{
    char callbox_id[64], broker[128], ssid[128], mqtt_user[64];
    html_escape(s_config->callbox_id, callbox_id, sizeof(callbox_id));
    html_escape(s_config->mqtt_broker, broker, sizeof(broker));
    html_escape(s_config->wifi_ssid, ssid, sizeof(ssid));
    html_escape(s_config->mqtt_user, mqtt_user, sizeof(mqtt_user));

    char page[6144];
    snprintf(page, sizeof(page),
             "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Callbox setup</title><style>*{box-sizing:border-box}body{margin:0;background:#eef2f7;color:#172033;font:14px system-ui,-apple-system,Segoe UI,Arial}main{max-width:680px;margin:auto;padding:18px}.top{background:#18263b;color:#fff;border-radius:16px;padding:20px 22px;margin-bottom:14px}.top h1{margin:0 0 4px;font-size:22px}.top p{margin:0;color:#b9c7da}.card{background:#fff;border:1px solid #dce3ed;border-radius:14px;padding:16px;margin:12px 0;box-shadow:0 3px 14px #1522380d}h2{font-size:16px;margin:0 0 12px}label{display:block;font-weight:600;margin:10px 0 5px}input{width:100%%;padding:11px 12px;border:1px solid #cbd5e1;border-radius:9px;font:inherit}input:focus{outline:2px solid #8ec5ff;border-color:#3182ce}.grid{display:grid;grid-template-columns:1fr 130px;gap:10px}.actions{display:flex;align-items:center;gap:10px;flex-wrap:wrap}button{border:0;border-radius:9px;padding:10px 14px;background:#1677d2;color:#fff;font-weight:700;cursor:pointer}button.secondary{background:#e8f1fb;color:#1260a8}button:disabled{opacity:.55}small,.hint{color:#66758a}.save{width:100%%;padding:13px;background:#0b8f68;margin-top:4px}</style></head><body><main>"
             "<header class='top'><h1>Callbox setup</h1><p>Configure one shared firmware for this unit</p></header><form method='post' action='/save'>"
             "<section class='card'><h2>Device</h2><label>Callbox ID</label><input name='callbox_id' value='%s' maxlength='15' required><p class='hint'>Use cb01, cb02... Each unit should have a unique ID.</p></section>"
             "<section class='card'><h2>Plant WiFi</h2><label>Network SSID</label><input id='wifi_ssid' name='wifi_ssid' list='wifi-list' value='%s' maxlength='32' required><datalist id='wifi-list'></datalist><div class='actions'><button type='button' class='secondary' id='scan' onclick='scanWifi()'>Scan nearby WiFi</button><span id='scan_status' class='hint'></span></div><label>Password</label><input type='password' name='wifi_pass' maxlength='63' placeholder='Blank keeps the saved password'><p class='hint'>Factory default: AGV1 / 123456789. Up to 5 networks are remembered.</p></section>"
             "<section class='card'><h2>MQTT server</h2><div class='grid'><div><label>Broker / IP</label><input name='mqtt_broker' value='%s' maxlength='63' required></div><div><label>Port</label><input type='number' name='mqtt_port' value='%u' min='1' max='65535' required></div></div><label>User</label><input name='mqtt_user' value='%s' maxlength='31'><label>Password</label><input type='password' name='mqtt_pass' maxlength='63' placeholder='Blank keeps the saved password'></section>"
             "<button class='save' type='submit'>Save and reboot</button></form><p class='hint'>Local AP: CALLBOX-%s &middot; password: CALLBOX-%s &middot; 192.168.4.1</p>"
             "<script>async function scanWifi(){let b=document.getElementById('scan'),s=document.getElementById('scan_status');b.disabled=true;s.textContent='Scanning...';try{let a=await (await fetch('/api/wifi-scan')).json(),d=document.getElementById('wifi-list');d.innerHTML='';a.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.label=x.ssid+' ('+x.rssi+' dBm)';d.appendChild(o)});s.textContent=a.length+' network(s) found';}catch(e){s.textContent='Scan failed';}b.disabled=false}</script></main></body></html>",
             callbox_id, ssid, broker, (unsigned)s_config->mqtt_port, mqtt_user, callbox_id, callbox_id);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

/* GET /api/config — trả cấu hình hiện tại dưới dạng JSON cho giao diện JS */
static esp_err_t config_json_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    char callbox_id[64], ssid[96], broker[128], user[64];
    char ip[32], netmask[32], gateway[32], dns[32];
    json_escape(s_config->callbox_id, callbox_id, sizeof(callbox_id));
    json_escape(s_config->wifi_ssid, ssid, sizeof(ssid));
    json_escape(s_config->mqtt_broker, broker, sizeof(broker));
    json_escape(s_config->mqtt_user, user, sizeof(user));
    json_escape(s_config->wifi_ip, ip, sizeof(ip));
    json_escape(s_config->wifi_netmask, netmask, sizeof(netmask));
    json_escape(s_config->wifi_gateway, gateway, sizeof(gateway));
    json_escape(s_config->wifi_dns, dns, sizeof(dns));

    char response[768];
    snprintf(response, sizeof(response),
             "{\"callbox_id\":\"%s\",\"wifi_ssid\":\"%s\","
             "\"wifi_dhcp\":%u,\"wifi_ip\":\"%s\",\"wifi_netmask\":\"%s\","
             "\"wifi_gateway\":\"%s\",\"wifi_dns\":\"%s\","
             "\"mqtt_broker\":\"%s\",\"mqtt_port\":%u,\"mqtt_user\":\"%s\"}",
             callbox_id, ssid, s_config->wifi_dhcp ? 1U : 0U,
             ip, netmask, gateway, dns, broker, (unsigned)s_config->mqtt_port, user);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

/* GET /api/status — trạng thái hệ thống cho giao diện: STA/SSID/RSSI/IP,
 * MQTT, AP, tên thiết bị và các topic MQTT đang dùng. */
static esp_err_t system_status_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;

    wifi_sta_status_t sta;
    wifi_get_sta_status(&sta);
    bsp_eth_status_t eth;
    bsp_eth_get_status(&eth);
    char ssid[96], ip[32], gateway[32], eth_ip[32], client_id[96];
    char cmd_topic[128], event_topic[128], status_topic[128];
    json_escape(sta.ssid, ssid, sizeof(ssid));
    json_escape(sta.ip, ip, sizeof(ip));
    json_escape(sta.gateway, gateway, sizeof(gateway));
    json_escape(eth.ip, eth_ip, sizeof(eth_ip));
    format_device_name(s_config->callbox_id, client_id, sizeof(client_id));
    snprintf(cmd_topic, sizeof(cmd_topic), MQTT_CMD_TOPIC "/#", s_config->callbox_id);
    snprintf(event_topic, sizeof(event_topic), MQTT_EVENT_TOPIC, s_config->callbox_id);
    snprintf(status_topic, sizeof(status_topic), MQTT_STATUS_TOPIC, s_config->callbox_id);

    char response[1024];
    snprintf(response, sizeof(response),
             "{\"sta\":%u,\"ssid\":\"%s\",\"rssi\":%d,"
             "\"ip\":\"%s\",\"gateway\":\"%s\",\"mqtt\":%u,"
             "\"eth\":%u,\"eth_ip\":\"%s\","
             "\"mqtt_transport\":\"%s\","
             "\"ap\":%u,\"client_id\":\"%s\","
             "\"topics\":{\"cmd\":\"%s\",\"event\":\"%s\",\"status\":\"%s\"}}",
             sta.connected ? 1U : 0U, ssid, (int)sta.rssi, ip, gateway,
             mqtt_is_connected() ? 1U : 0U,
             eth.connected ? 1U : 0U, eth_ip,
             s_config->mqtt_transport == MQTT_TRANSPORT_TLS ? "tls" : "tcp",
             wifi_ap_is_active() ? 1U : 0U,
             client_id, cmd_topic, event_topic, status_topic);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

/* GET /api/wifi-profiles — danh sách mạng WiFi đã nhớ kèm cờ "active"
 * (mạng đang dùng) để giao diện hiển thị và quản lý. */
static esp_err_t wifi_profiles_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    /* Profile ưa thích (s_config->wifi_ssid) chưa chắc là mạng driver Wi-Fi
     * đang chọn: driver có thể chọn AP nhớ có tín hiệu mạnh hơn. Đọc SSID
     * STA lúc chạy để làm nhãn trên giao diện. */
    wifi_sta_status_t sta = {0};
    wifi_get_sta_status(&sta);
    char response[1024] = "{\"profiles\":[";
    size_t used = strlen(response);
    const uint8_t count = s_config->wifi_profile_count > MAX_WIFI_PROFILES
                              ? MAX_WIFI_PROFILES : s_config->wifi_profile_count;
    for (uint8_t i = 0; i < count && used + 80 < sizeof(response); ++i) {
        char ssid[96];
        json_escape(s_config->wifi_profiles[i].ssid, ssid, sizeof(ssid));
        int written = snprintf(response + used, sizeof(response) - used,
                               "%s{\"ssid\":\"%s\",\"active\":%u}",
                               i ? "," : "", ssid,
                               sta.connected &&
                               strcmp(sta.ssid, s_config->wifi_profiles[i].ssid) == 0 ? 1U : 0U);
        if (written < 0 || (size_t)written >= sizeof(response) - used) break;
        used += (size_t)written;
    }
    snprintf(response + used, sizeof(response) - used, "]}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

/* POST /api/wifi-profiles/delete — xóa 1 mạng nhớ theo SSID (body form).
 * Cập nhật NVS + áp dụng lại cấu hình WiFi ngay (không cần khởi động lại). */
static esp_err_t wifi_profile_delete_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    /* Đọc thân form (tối đa 128 byte) để lấy SSID cần xóa */
    if (req->content_len == 0 || req->content_len >= 128) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi profile request");
        return ESP_OK;
    }
    char body[128] = { 0 };
    if (portal_receive_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request read timed out");
        return ESP_OK;
    }
    char ssid[33] = { 0 };
    if (!form_value(body, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID is required");
        return ESP_OK;
    }
    /* Xóa trên bản sao cấu hình; nếu không tìm thấy SSID → 404 */
    Config_t updated = *s_config;
    if (!config_remove_wifi_profile(&updated, ssid)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Wi-Fi profile not found");
        return ESP_OK;
    }
    /* Lưu xuống NVS, rồi gắn lại bản đã cập nhật vào cấu hình chung */
    esp_err_t err = callbox_config_store_save(&updated);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save Wi-Fi profiles");
        return ESP_OK;
    }
    *s_config = updated;
    /* Áp dụng WiFi ngay để loại mạng vừa xóa ra khỏi lựa chọn kết nối */
    (void)wifi_apply_config(&updated);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* GET /api/io-status — trạng thái I/O thực tế (nút bấm, tháp đèn, LED,
 * buzzer, LED AP) dưới dạng JSON cho màn hình giám sát trên portal. */
static esp_err_t io_status_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    io_debug_snapshot_t snapshot;
    /* Đọc trạng thái DI/DO từ lớp phần mềm (io_debug) */
    io_debug_read(&snapshot);
    const callbox_io_mapping_t *m = callbox_io_get_mapping();

    /* Trích bit trạng thái theo mapping: 3 nút (DI), 3 màu tháp đèn (DO),
     * 3 LED nút (DO) — mỗi phần tử 0/1. */
    unsigned buttons[3] = {
        (snapshot.di_active_mask >> m->btn_task1) & 1u,
        (snapshot.di_active_mask >> m->btn_task2) & 1u,
        (snapshot.di_active_mask >> m->btn_cancel) & 1u,
    };
    unsigned tower[3] = {
        (snapshot.do_active_mask >> m->tower_red) & 1u,
        (snapshot.do_active_mask >> m->tower_yellow) & 1u,
        (snapshot.do_active_mask >> m->tower_green) & 1u,
    };
    unsigned button_leds[3] = {
        (snapshot.do_active_mask >> m->led_task1) & 1u,
        (snapshot.do_active_mask >> m->led_task2) & 1u,
        (snapshot.do_active_mask >> m->led_cancel) & 1u,
    };

    snprintf(s_io_status_json, sizeof(s_io_status_json),
             "{\"di_mask\":%u,\"do_mask\":%u,"
             "\"buttons\":[%u,%u,%u],"
             "\"inputs\":[%u,%u,%u,%u,%u,%u,%u,%u],"
             "\"outputs\":[%u,%u,%u,%u,%u,%u,%u,%u],"
             "\"tower\":{\"red\":%u,\"yellow\":%u,\"green\":%u},"
             "\"button_leds\":[%u,%u,%u],\"buzzer\":%u,\"ap_led\":%u}",
             (unsigned)snapshot.di_active_mask, (unsigned)snapshot.do_active_mask,
             buttons[0], buttons[1], buttons[2],
             (snapshot.di_active_mask >> 0) & 1u, (snapshot.di_active_mask >> 1) & 1u,
             (snapshot.di_active_mask >> 2) & 1u, (snapshot.di_active_mask >> 3) & 1u,
             (snapshot.di_active_mask >> 4) & 1u, (snapshot.di_active_mask >> 5) & 1u,
             (snapshot.di_active_mask >> 6) & 1u, (snapshot.di_active_mask >> 7) & 1u,
             (snapshot.do_active_mask >> 0) & 1u, (snapshot.do_active_mask >> 1) & 1u,
             (snapshot.do_active_mask >> 2) & 1u, (snapshot.do_active_mask >> 3) & 1u,
             (snapshot.do_active_mask >> 4) & 1u, (snapshot.do_active_mask >> 5) & 1u,
             (snapshot.do_active_mask >> 6) & 1u, (snapshot.do_active_mask >> 7) & 1u,
             tower[0], tower[1], tower[2], button_leds[0], button_leds[1], button_leds[2],
             (snapshot.do_active_mask >> m->buzzer) & 1u,
             wifi_ap_is_active() ? 1u : 0u);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_io_status_json, HTTPD_RESP_USE_STRLEN);
}

/* POST /api/session/open — khi trang cấu hình mở, "khóa" phiên: gia hạn
 * deadline AP để network_status_task không tắt AP khi đang cấu hình. */
static esp_err_t session_open_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    /* Only a client actually connected through the local AP may keep the AP
     * alive. A logged-in STA browser must not prevent AP auto-shutdown. */
    if (request_from_local_ap(req)) {
        s_session_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(PORTAL_SESSION_TIMEOUT_MS);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"timeout_s\":30}");
}

/* POST /api/session/ping — heartbeat của trang: gia hạn phiên CHỈ KHI phiên
 * vẫn còn hoạt động. Phiên đã đóng (finish) phải giữ đóng — heartbeat muộn
 * của trình duyệt không được mở lại AP và giữ đèn trạng thái AP bật. */
static esp_err_t session_ping_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    /* Phiên cấu hình đã kết thúc phải giữ đóng. Heartbeat trang có thể còn
     * đến vài giây trong lúc đóng trình duyệt; nó không được mở lại phiên AP
     * và giữ đầu ra trạng thái AP hoạt động. */
    if (request_from_local_ap(req) && config_portal_session_active()) {
        s_session_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(PORTAL_SESSION_TIMEOUT_MS);
    }
    return httpd_resp_sendstr(req, "OK");
}

/* POST /api/session/finish — kết thúc phiên: đóng ngay hạn phiên (
 * s_session_deadline = 0), để network_status_task có quyền tắt AP sau đó. */
static esp_err_t session_finish_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    s_session_deadline = 0;
    return httpd_resp_sendstr(req, "OK");
}

/* Query trạng thái phiên: còn hoạt động nếu hạn chưa trôi và khác 0 */
bool config_portal_session_active(void)
{
    TickType_t deadline = s_session_deadline;
    if (deadline == 0) return false;
    return (int32_t)(deadline - xTaskGetTickCount()) > 0;
}

/* (Không dùng trực tiếp) Trang cấu hình chính — bản HTML nhúng Tiếng Việt.
 * Dựng danh sách giá trị đã HTML-escape từ cấu hình rồi đổ vào template. */
static esp_err_t portal_page_handler(httpd_req_t *req)
{
    char callbox[64], ssid[96], broker[128], user[64];
    char ip[32], netmask[32], gateway[32], dns[32];
    html_escape(s_config->callbox_id, callbox, sizeof(callbox));
    html_escape(s_config->wifi_ssid, ssid, sizeof(ssid));
    html_escape(s_config->mqtt_broker, broker, sizeof(broker));
    html_escape(s_config->mqtt_user, user, sizeof(user));
    html_escape(s_config->wifi_ip, ip, sizeof(ip));
    html_escape(s_config->wifi_netmask, netmask, sizeof(netmask));
    html_escape(s_config->wifi_gateway, gateway, sizeof(gateway));
    html_escape(s_config->wifi_dns, dns, sizeof(dns));

    snprintf(s_page_html, sizeof(s_page_html),
             "<!doctype html><html lang='vi'><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>C&#xE0;i &#x0111;&#x1EB7;t Callbox</title><style>"
             ":root{--navy:#14263d;--blue:#1b6fd1;--teal:#0b8f68;--ink:#172033;--muted:#64748b;--line:#d8e0ea;--bg:#f3f6fa;--card:#fff;--danger:#b42318}"
             "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,Segoe UI,Arial,sans-serif}main{max-width:720px;margin:0 auto;padding:20px 16px 44px}.hero{background:linear-gradient(135deg,var(--navy),#1d456b);color:#fff;border-radius:18px;padding:24px;margin-bottom:16px;box-shadow:0 8px 26px #14263d26}.brand{display:flex;align-items:center;gap:12px}.mark{width:42px;height:42px;border-radius:12px;background:#52d3ad;color:#0b382d;display:grid;place-items:center;font-weight:800;font-size:20px}.hero h1{font-size:24px;line-height:1.15;margin:0}.hero p{margin:7px 0 0;color:#d9e7f4}.status{display:flex;align-items:center;gap:8px;margin-top:18px;font-size:14px;color:#d9e7f4}.dot{width:9px;height:9px;background:#52d3ad;border-radius:50%%;box-shadow:0 0 0 4px #52d3ad2e}.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px;margin:14px 0;box-shadow:0 4px 16px #14263d0b}.card h2{font-size:17px;margin:0 0 4px}.card>p{color:var(--muted);font-size:14px;margin:0 0 13px}.field{margin:12px 0}.field label{display:block;font-weight:650;font-size:14px;margin-bottom:6px}.field input,.field select{width:100%%;min-height:46px;border:1px solid #bac8d8;border-radius:10px;padding:10px 12px;background:#fff;color:var(--ink);font:inherit}.field input:focus,.field select:focus{outline:3px solid #9ec9ff;border-color:var(--blue)}.grid{display:grid;grid-template-columns:1fr 150px;gap:12px}.actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:8px}.btn{min-height:46px;border:0;border-radius:10px;padding:10px 16px;font:650 15px inherit;cursor:pointer;transition:filter .18s,transform .18s}.btn:hover{filter:brightness(1.05)}.btn:active{transform:translateY(1px)}.btn:disabled{opacity:.6;cursor:wait}.primary{background:var(--teal);color:#fff}.secondary{background:#e8f1fb;color:#145d9f}.ghost{background:transparent;color:#145d9f;border:1px solid #aac5df}.save-row{display:flex;gap:10px;margin-top:18px}.save-row .btn{flex:1}.message{min-height:24px;margin:12px 0 0;font-size:14px}.message.ok{color:#087f5b}.message.err{color:var(--danger)}.hint{color:var(--muted);font-size:13px}.hidden{display:none!important}datalist{max-height:200px}@media(max-width:520px){main{padding:12px 12px 36px}.hero{padding:20px}.grid{grid-template-columns:1fr}.save-row{flex-direction:column}.btn{width:100%%}}@media(prefers-reduced-motion:reduce){.btn{transition:none}}"
             "</style></head><body><main><header class='hero'><div class='brand'><div class='mark'>C</div><div><h1>Callbox setup</h1><p>Configure this unit for plant operation</p></div></div><div class='status'><span class='dot'></span>Local setup AP is active · 192.168.4.1</div></header>"
             "<form id='f'><section class='card'><h2>Device identity</h2><p>Use a logical ID that the operator can replace if the hardware is changed.</p><div class='field'><label for='callbox_id'>Callbox ID</label><input id='callbox_id' name='callbox_id' value='%s' maxlength='15' required></div></section>"
             "<section class='card'><h2>Plant WiFi</h2><p>The unit remembers up to five networks and prefers the strongest visible one.</p><div class='field'><label for='ssid'>Network SSID</label><input id='ssid' name='wifi_ssid' list='wifi-list' value='%s' maxlength='32'><datalist id='wifi-list'></datalist><div class='actions'><button class='btn secondary' type='button' id='scan'>Scan nearby networks</button><span id='scanmsg' class='hint'></span></div></div><div class='field'><label for='wifi_pass'>Password</label><input id='wifi_pass' type='password' name='wifi_pass' maxlength='63' placeholder='Blank keeps the saved password'></div><div class='field'><label for='wifi_dhcp'>IP mode</label><select id='wifi_dhcp' name='wifi_dhcp'><option value='1' %s>Automatic (DHCP)</option><option value='0' %s>Manual (static IP)</option></select></div><div id='static' class='%s'><div class='grid'><div class='field'><label for='wifi_ip'>IP address</label><input id='wifi_ip' name='wifi_ip' value='%s' placeholder='192.168.1.20'></div><div class='field'><label for='wifi_netmask'>Netmask</label><input id='wifi_netmask' name='wifi_netmask' value='%s' placeholder='255.255.255.0'></div></div><div class='grid'><div class='field'><label for='wifi_gateway'>Gateway</label><input id='wifi_gateway' name='wifi_gateway' value='%s' placeholder='192.168.1.1'></div><div class='field'><label for='wifi_dns'>DNS</label><input id='wifi_dns' name='wifi_dns' value='%s' placeholder='192.168.1.1'></div></div></div></section>"
             "<section class='card'><h2>MQTT server</h2><p>Set the broker used by the callbox firmware.</p><div class='grid'><div class='field'><label for='mqtt_broker'>Broker / IP</label><input id='mqtt_broker' name='mqtt_broker' value='%s' maxlength='63' required></div><div class='field'><label for='mqtt_port'>Port</label><input id='mqtt_port' name='mqtt_port' type='number' value='%u' min='1' max='65535' required></div></div><div class='field'><label for='mqtt_user'>Username</label><input id='mqtt_user' name='mqtt_user' value='%s' maxlength='31'></div><div class='field'><label for='mqtt_pass'>Password</label><input id='mqtt_pass' name='mqtt_pass' type='password' maxlength='63' placeholder='Blank keeps the saved password'></div></section>"
             "<div class='save-row'><button class='btn primary' id='save' type='submit'>Save configuration</button><button class='btn ghost' id='finish' type='button'>Finish setup</button></div><p id='msg' class='message' role='status' aria-live='polite'></p><p class='hint'>Saving applies WiFi without rebooting. Keep this page open until the save confirmation appears.</p></form></main>"
             "<script>const f=document.getElementById('f'),m=document.getElementById('msg'),dhcp=document.getElementById('wifi_dhcp'),st=document.getElementById('static'),save=document.getElementById('save');function sync(){st.classList.toggle('hidden',dhcp.value==='1')}dhcp.onchange=sync;sync();async function ping(){fetch('/api/session/ping',{method:'POST'}).catch(()=>{})}fetch('/api/session/open',{method:'POST'}).catch(()=>{});setInterval(ping,5000);document.getElementById('scan').onclick=async()=>{let b=document.getElementById('scan'),t=document.getElementById('scanmsg');b.disabled=true;t.textContent='Scanning...';try{let a=await(await fetch('/api/wifi-scan')).json(),d=document.getElementById('wifi-list');d.innerHTML='';a.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.label=x.ssid+' ('+x.rssi+' dBm)';d.appendChild(o)});t.textContent=a.length+' network(s) found';}catch(e){t.textContent='Scan failed';}b.disabled=false};f.onsubmit=async e=>{e.preventDefault();save.disabled=true;m.className='message';m.textContent='Saving...';try{let r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))});m.className=r.ok?'message ok':'message err';m.textContent=await r.text()}catch(e){m.className='message err';m.textContent='Save failed. Check the AP connection.'}save.disabled=false};document.getElementById('finish').onclick=async()=>{await fetch('/api/session/finish',{method:'POST'});m.className='message ok';m.textContent='Setup finished. The AP remains available for recovery.'};window.addEventListener('pagehide',()=>{try{navigator.sendBeacon('/api/session/finish','')}catch(e){}});</script></body></html>",
             callbox, ssid, s_config->wifi_dhcp ? "selected" : "", s_config->wifi_dhcp ? "" : "selected",
             s_config->wifi_dhcp ? "hidden" : "", ip, netmask, gateway, dns,
             broker, (unsigned)s_config->mqtt_port, user);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_page_html, HTTPD_RESP_USE_STRLEN);
}

/* (Không dùng) Trang cấu hình Tiếng Việt phiên bản mobile-first cũ.
 * Giữ nguyên nội tuyến, KHÔNG phụ thuộc Internet — hoạt động tốt khi AP
 * không có kết nối. Đây là bản legacy của portal_page_handler_vi. */
static esp_err_t __attribute__((unused)) portal_page_handler_vi_legacy(httpd_req_t *req)
{
    char callbox[64], ssid[96], broker[128], user[64];
    char ip[32], netmask[32], gateway[32], dns[32];
    html_escape(s_config->callbox_id, callbox, sizeof(callbox));
    html_escape(s_config->wifi_ssid, ssid, sizeof(ssid));
    html_escape(s_config->mqtt_broker, broker, sizeof(broker));
    html_escape(s_config->mqtt_user, user, sizeof(user));
    html_escape(s_config->wifi_ip, ip, sizeof(ip));
    html_escape(s_config->wifi_netmask, netmask, sizeof(netmask));
    html_escape(s_config->wifi_gateway, gateway, sizeof(gateway));
    html_escape(s_config->wifi_dns, dns, sizeof(dns));

    snprintf(s_page_html, sizeof(s_page_html),
             "<!doctype html><html lang='vi'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>C&#xE0;i &#x0111;&#x1EB7;t Callbox</title><style>"
             ":root{--navy:#202326;--blue:#2455f4;--teal:#2455f4;--ink:#172033;--muted:#64748b;--line:#d8e0ea;--bg:#f3f6fa;--card:#fff;--danger:#b42318}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,Segoe UI,Arial,sans-serif}main{max-width:720px;margin:auto;padding:20px 16px 44px}.hero{background:linear-gradient(135deg,var(--navy),#35404a);color:#fff;border-radius:18px;padding:24px;margin-bottom:16px;box-shadow:0 8px 26px #20232626}.brand{display:flex;align-items:center;gap:12px}.logo{display:block;width:min(190px,45vw);height:auto;max-height:42px;object-fit:contain;background:#fff;border-radius:8px;padding:5px 10px}.hero h1{font-size:24px;line-height:1.15;margin:0}.hero p{margin:7px 0 0;color:#dce7ff}.status{display:flex;align-items:center;gap:8px;margin-top:18px;font-size:14px;color:#dce7ff}.dot{width:9px;height:9px;background:#3b82f6;border-radius:50%%;box-shadow:0 0 0 4px #3b82f633}.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px;margin:14px 0;box-shadow:0 4px 16px #2023260b}.card h2{font-size:17px;margin:0 0 4px}.card>p{color:var(--muted);font-size:14px;margin:0 0 13px}.field{margin:12px 0}.field label{display:block;font-weight:650;font-size:14px;margin-bottom:6px}.field input,.field select{width:100%%;min-height:46px;border:1px solid #bac8d8;border-radius:10px;padding:10px 12px;background:#fff;color:var(--ink);font:inherit}.field input:focus,.field select:focus{outline:3px solid #9ec9ff;border-color:var(--blue)}.grid{display:grid;grid-template-columns:1fr 150px;gap:12px}.actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:8px}.btn{min-height:46px;border:0;border-radius:10px;padding:10px 16px;font:650 15px inherit;cursor:pointer;transition:filter .18s,transform .18s}.btn:hover{filter:brightness(1.05)}.btn:active{transform:translateY(1px)}.btn:disabled{opacity:.6;cursor:wait}.primary{background:var(--teal);color:#fff}.secondary{background:#e8f1fb;color:#145d9f}.ghost{background:transparent;color:#145d9f;border:1px solid #aac5df}.save-row{display:flex;gap:10px;margin-top:18px}.save-row .btn{flex:1}.message{min-height:24px;margin:12px 0 0;font-size:14px}.message.ok{color:#087f5b}.message.err{color:var(--danger)}.hint{color:var(--muted);font-size:13px}.hidden{display:none!important}@media(max-width:520px){main{padding:12px}.hero{padding:20px}.grid{grid-template-columns:1fr}.save-row{flex-direction:column}.btn{width:100%%}}@media(prefers-reduced-motion:reduce){.btn{transition:none}}"
             ".card>p{display:none}.save-row~.hint{display:none}.card{padding:14px;margin:10px 0}.field{margin:10px 0}</style></head><body><main><header class='hero'><div class='brand'><img class='logo' src='/logo.jpg' alt='AUBOT'><div><h1>C&#xE0;i &#x0111;&#x1EB7;t Callbox</h1><p>C&#x1EA5;u h&#xEC;nh thi&#x1EBF;t b&#x1ECB; cho v&#x1EAD;n h&#xE0;nh nh&#xE0; m&#xE1;y</p></div></div><div class='status'><span class='dot'></span>AP c&#x1EA5;u h&#xEC;nh &#x111;ang b&#x1EAD;t &middot; 192.168.4.1</div></header>"
             "<form id='f'><div class='layout'><section class='card'><h2>Nh&#x1EAD;n d&#x1EA1;ng thi&#x1EBF;t b&#x1ECB;</h2><p>D&#xF9;ng ID logic c&#xF3; th&#x1EC3; thay &#x0111;&#x1ED5;i khi thay ph&#x1EA7;n c&#x1EE9;ng.</p><div class='field'><label for='callbox_id'>ID Callbox</label><input id='callbox_id' name='callbox_id' value='%s' maxlength='15' required></div></section>"
             "<section class='card'><h2>WiFi nh&#xE0; m&#xE1;y</h2><p>Thi&#x1EBF;t b&#x1ECB; ghi nh&#x1EDB t&#x1ED1;i &#x0111;a 5 m&#x1EA1;ng v&#xE0; &#x01B0;u ti&#xEA;n m&#x1EA1;ng c&#xF3; t&#xED;n hi&#x1EC7;u m&#x1EA1;nh nh&#x1EA5;t.</p><div class='field'><label for='ssid'>SSID m&#x1EA1;ng</label><input id='ssid' name='wifi_ssid' list='wifi-list' value='%s' maxlength='32'><datalist id='wifi-list'></datalist><div class='actions'><button class='btn secondary' type='button' id='scan'>Qu&#xE9;t m&#x1EA1;ng WiFi</button><span id='scanmsg' class='hint'></span></div></div><div class='field'><label for='wifi_pass'>M&#x1EAD;t kh&#x1EA9;u</label><input id='wifi_pass' type='password' name='wifi_pass' maxlength='63' placeholder='&#x110;&#x1EC3; tr&#x1ED1;ng &#x0111;&#x1EC3; gi&#x1EEF; m&#x1EAD;t kh&#x1EA9;u c&#x169;'></div><div class='field'><label for='wifi_dhcp'>Ch&#x1EBF; &#x0111;&#x1ED9; IP</label><select id='wifi_dhcp' name='wifi_dhcp'><option value='1' %s>T&#x1EF1; &#x0111;&#x1ED9;ng (DHCP)</option><option value='0' %s>Th&#x1EE7; c&#xF4;ng (IP t&#x0129;nh)</option></select></div><div id='static' class='%s'><div class='grid'><div class='field'><label for='wifi_ip'>&#x0110;&#x1ECB;a ch&#x1EC9; IP</label><input id='wifi_ip' name='wifi_ip' value='%s' placeholder='192.168.1.20'></div><div class='field'><label for='wifi_netmask'>Netmask</label><input id='wifi_netmask' name='wifi_netmask' value='%s' placeholder='255.255.255.0'></div></div><div class='grid'><div class='field'><label for='wifi_gateway'>Gateway</label><input id='wifi_gateway' name='wifi_gateway' value='%s' placeholder='192.168.1.1'></div><div class='field'><label for='wifi_dns'>DNS</label><input id='wifi_dns' name='wifi_dns' value='%s' placeholder='192.168.1.1'></div></div></div></section>"
             "<section class='card'><h2>M&#xE1;y ch&#x1EE7; MQTT</h2><p>C&#xE0;i &#x0111;&#x1ECB;a ch&#x1EC9; broker m&#xE0; firmware Callbox s&#x1EED; d&#x1EE5;ng.</p><div class='grid'><div class='field'><label for='mqtt_broker'>Broker / IP</label><input id='mqtt_broker' name='mqtt_broker' value='%s' maxlength='63' required></div><div class='field'><label for='mqtt_port'>C&#x1ED5;ng (Port)</label><input id='mqtt_port' name='mqtt_port' type='number' value='%u' min='1' max='65535' required></div></div><div class='field'><label for='mqtt_user'>T&#xE0;i kho&#x1EA3;n</label><input id='mqtt_user' name='mqtt_user' value='%s' maxlength='31'></div><div class='field'><label for='mqtt_pass'>M&#x1EAD;t kh&#x1EA9;u</label><input id='mqtt_pass' name='mqtt_pass' type='password' maxlength='63' placeholder='&#x110;&#x1EC3; tr&#x1ED1;ng &#x0111;&#x1EC3; gi&#x1EEF; m&#x1EAD;t kh&#x1EA9;u c&#x169;'></div></section>"
             "<div class='save-row'><button class='btn primary' id='save' type='submit'>L&#x01B0;u c&#x1EA5;u h&#xEC;nh</button><button class='btn ghost' id='finish' type='button'>K&#x1EBF;t th&#xFA;c c&#xE0;i &#x0111;&#x1EB7;t</button></div><p id='msg' class='message' role='status' aria-live='polite'></p><p class='hint'>C&#xE0;i &#x0111;&#x1EB7;t WiFi &#x0111;&#x01B0;&#x1EE3;c &#xE1;p d&#x1EE5;ng m&#xE0; kh&#xF4;ng kh&#x1EDF;i &#x0111;&#x1ED9;ng l&#x1EA1;i. H&#xE3;y gi&#x1EEF; trang m&#x1EDF; &#x0111;&#x1EBF;n khi c&#xF3; th&#xF4;ng b&#xE1;o th&#xE0;nh c&#xF4;ng.</p></form></main>"
             "<script>const f=document.getElementById('f'),m=document.getElementById('msg'),dhcp=document.getElementById('wifi_dhcp'),st=document.getElementById('static'),save=document.getElementById('save');function sync(){st.classList.toggle('hidden',dhcp.value==='1')}dhcp.onchange=sync;sync();async function ping(){fetch('/api/session/ping',{method:'POST'}).catch(()=>{})}fetch('/api/session/open',{method:'POST'}).catch(()=>{});setInterval(ping,5000);document.getElementById('scan').onclick=async()=>{let b=document.getElementById('scan'),t=document.getElementById('scanmsg');b.disabled=true;t.textContent='Dang quet...';try{let a=await(await fetch('/api/wifi-scan')).json(),d=document.getElementById('wifi-list');d.innerHTML='';a.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.label=x.ssid+' ('+x.rssi+' dBm)';d.appendChild(o)});t.textContent=a.length+' mang duoc tim thay';}catch(e){t.textContent='Quet that bai';}b.disabled=false};f.onsubmit=async e=>{e.preventDefault();save.disabled=true;m.className='message';m.textContent='Dang luu...';try{let r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))});m.className=r.ok?'message ok':'message err';m.textContent=await r.text()}catch(e){m.className='message err';m.textContent='Luu that bai. Hay kiem tra ket noi AP.'}save.disabled=false};document.getElementById('finish').onclick=async()=>{await fetch('/api/session/finish',{method:'POST'});m.className='message ok';m.textContent='Da ket thuc cai dat. AP van san sang de khoi phuc.'};window.addEventListener('pagehide',()=>{try{navigator.sendBeacon('/api/session/finish','')}catch(e){}});</script></body></html>",
             callbox, ssid, s_config->wifi_dhcp ? "selected" : "", s_config->wifi_dhcp ? "" : "selected",
             s_config->wifi_dhcp ? "hidden" : "", ip, netmask, gateway, dns,
             broker, (unsigned)s_config->mqtt_port, user);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_page_html, HTTPD_RESP_USE_STRLEN);
}

/* Trang cấu hình Tiếng Việt (bản đang dùng). Sau khi đổ template, thực hiện
 * biến đổi HTML tại chỗ: thay IP cũ, chèn trạng thái hero, stylesheet final,
 * và nâng cấp asset của logo để tránh cache cũ của trình duyệt. */
static esp_err_t portal_page_handler_vi(httpd_req_t *req)
{
    char callbox[64], ssid[96], broker[128], user[64];
    char ip[32], netmask[32], gateway[32], dns[32];
    html_escape(s_config->callbox_id, callbox, sizeof(callbox));
    html_escape(s_config->wifi_ssid, ssid, sizeof(ssid));
    html_escape(s_config->mqtt_broker, broker, sizeof(broker));
    html_escape(s_config->mqtt_user, user, sizeof(user));
    html_escape(s_config->wifi_ip, ip, sizeof(ip));
    html_escape(s_config->wifi_netmask, netmask, sizeof(netmask));
    html_escape(s_config->wifi_gateway, gateway, sizeof(gateway));
    html_escape(s_config->wifi_dns, dns, sizeof(dns));

    snprintf(s_page_html, sizeof(s_page_html),
             "<!doctype html><html lang='vi'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>C&#xE0;i &#x0111;&#x1EB7;t Callbox</title><style>"
             ":root{--bg0:#080d27;--bg1:#121b43;--glass:rgba(25,36,80,.72);--line:rgba(170,193,255,.27);--text:#f7f8ff;--muted:#b9c5e2;--blue:#4f7cff;--violet:#a855f7;--cyan:#22d3ee;--danger:#ff9eb1}*{box-sizing:border-box}html{background:var(--bg0)}body{margin:0;min-height:100vh;background:radial-gradient(circle at 8%% 10%%,rgba(168,85,247,.25),transparent 34%%),radial-gradient(circle at 92%% 20%%,rgba(34,211,238,.18),transparent 32%%),linear-gradient(140deg,var(--bg0),var(--bg1));color:var(--text);font:16px/1.5 system-ui,-apple-system,Segoe UI,Arial,sans-serif}main{max-width:980px;margin:auto;padding:24px 18px 48px}.hero{position:relative;overflow:hidden;background:linear-gradient(135deg,rgba(27,34,76,.86),rgba(26,47,109,.68));border:1px solid var(--line);border-radius:22px;padding:20px 22px;margin-bottom:16px;box-shadow:0 18px 45px rgba(0,0,0,.28);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px)}.hero:after{content:'';position:absolute;width:220px;height:220px;right:-90px;top:-120px;border-radius:50%%;background:rgba(79,124,255,.28);filter:blur(4px)}.brand{position:relative;z-index:1;display:flex;align-items:center;gap:14px}.logo{display:block;width:min(180px,42vw);height:auto;max-height:42px;object-fit:contain;background:#fff;border-radius:9px;padding:5px 10px}.brand-copy{min-width:0}.eyebrow{font-size:11px;letter-spacing:.16em;color:#a9bbff;text-transform:uppercase}.hero h1{font-size:26px;line-height:1.15;margin:3px 0 0}.hero p{margin:5px 0 0;color:#dce7ff}.status{position:relative;z-index:1;display:flex;align-items:center;gap:9px;margin-top:16px;font-size:14px;color:#e1e8ff}.status-chip{padding:6px 10px;border:1px solid rgba(170,193,255,.23);border-radius:999px;background:rgba(8,13,39,.35)}.dot{width:9px;height:9px;flex:none;background:var(--cyan);border-radius:50%%;box-shadow:0 0 0 4px rgba(34,211,238,.18)}.layout{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{position:relative;overflow:hidden;background:var(--glass);border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 14px 32px rgba(0,0,0,.22);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px)}.card:before{content:'';position:absolute;left:18px;right:18px;top:0;height:2px;background:linear-gradient(90deg,var(--violet),var(--blue),var(--cyan));opacity:.82}.card h2{display:flex;align-items:center;gap:9px;font-size:17px;margin:0 0 10px}.step{display:grid;place-items:center;width:28px;height:28px;border-radius:9px;background:linear-gradient(135deg,var(--violet),var(--blue));color:#fff;font-size:12px;letter-spacing:.04em;box-shadow:0 5px 16px rgba(79,124,255,.28)}.card>p{display:none}.field{margin:12px 0}.field label{display:block;color:#eef2ff;font-weight:650;font-size:14px;margin-bottom:6px}.field input,.field select{width:100%%;min-height:48px;border:1px solid rgba(170,193,255,.27);border-radius:12px;padding:11px 13px;background:rgba(6,12,38,.58);color:var(--text);font:inherit}.field input::placeholder{color:#8492b7}.field input:focus,.field select:focus{outline:3px solid rgba(79,124,255,.26);border-color:var(--blue)}select option{background:#111b43;color:#f7f8ff}.grid{display:grid;grid-template-columns:1fr 150px;gap:12px}.actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:8px}.hint{color:#aebbdc;font-size:13px}.btn{min-height:46px;border:1px solid transparent;border-radius:12px;padding:10px 16px;font:650 15px inherit;cursor:pointer;transition:filter .18s,transform .18s,box-shadow .18s}.btn:hover{filter:brightness(1.08)}.btn:active{transform:translateY(1px)}.btn:focus-visible{outline:3px solid rgba(34,211,238,.55);outline-offset:2px}.btn:disabled{opacity:.6;cursor:wait}.primary{background:linear-gradient(135deg,#3f6fff,#8b4de8);color:#fff;box-shadow:0 10px 24px rgba(79,124,255,.26)}.secondary{background:rgba(79,124,255,.18);color:#dce7ff;border-color:rgba(139,176,255,.3)}.ghost{background:rgba(8,13,39,.28);color:#dce7ff;border-color:rgba(170,193,255,.35)}.save-row{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:16px}.save-row .btn{min-height:48px}.message{min-height:24px;margin:12px 0 0;font-size:14px}.message.ok{color:#6df5c6}.message.err{color:var(--danger)}.save-row~.hint{display:none}.hidden{display:none!important}@media(max-width:760px){main{padding:14px 12px 34px}.hero{padding:18px}.brand{align-items:flex-start}.logo{width:min(160px,42vw)}.hero h1{font-size:22px}.layout{grid-template-columns:1fr}.grid{grid-template-columns:1fr}.save-row{grid-template-columns:1fr}.btn{width:100%%}}@media(prefers-reduced-motion:reduce){*{transition:none!important}}</style></head><body><main>"
             "<header class='hero'><div class='brand'><img class='logo' src='/logo.jpg' alt='AUBOT'><div class='brand-copy'><div class='eyebrow'>AUBOT · CALLBOX</div><h1>C&#xE0;i &#x0111;&#x1EB7;t Callbox</h1><p>C&#x1EA5;u h&#xEC;nh thi&#x1EBF;t b&#x1ECB; cho v&#x1EAD;n h&#xE0;nh nh&#xE0; m&#xE1;y</p></div></div><div class='status'><span class='dot'></span><span class='status-chip'>AP c&#x1EA5;u h&#xEC;nh &#x111;ang b&#x1EAD;t &middot; 192.168.4.1</span></div></header>"
             "<form id='f'><div class='layout'><section class='card'><h2><span class='step'>01</span>Nh&#x1EAD;n d&#x1EA1;ng thi&#x1EBF;t b&#x1ECB;</h2><p>D&#xF9;ng ID logic c&#xF3; th&#x1EC3; thay &#x0111;&#x1ED5;i khi thay ph&#x1EA7;n c&#x1EE9;ng.</p><div class='field'><label for='callbox_id'>ID Callbox</label><input id='callbox_id' name='callbox_id' value='%s' maxlength='15' required></div></section>"
             "<section class='card'><h2><span class='step'>02</span>WiFi nh&#x00E0; m&#x00E1;y</h2><p>Thi&#x1EBF;t b&#x1ECB; ghi nh&#x1EDB t&#x1ED1;i &#x0111;a 5 m&#x1EA1;ng v&#x00E0; &#x01B0;u ti&#x00EA;n m&#x1EA1;ng c&#x00F3; t&#x00ED;n hi&#x1EC7;u m&#x1EA1;nh nh&#x1EA5;t.</p><div class='field'><label for='ssid'>SSID m&#x1EA1;ng</label><input id='ssid' name='wifi_ssid' list='wifi-list' value='%s' maxlength='32'><datalist id='wifi-list'></datalist><div class='actions'><button class='btn secondary' type='button' id='scan'>Qu&#x00E9;t m&#x1EA1;ng WiFi</button><span id='scanmsg' class='hint'></span></div></div><div class='field'><label for='wifi_pass'>M&#x1EAD;t kh&#x1EA9;u</label><input id='wifi_pass' type='password' name='wifi_pass' maxlength='63' placeholder='&#x110;&#x1EC3; tr&#x1ED1;ng &#x0111;&#x1EC3; gi&#x1EEF; m&#x1EAD;t kh&#x1EA9;u c&#x169;'></div><div class='field'><label for='wifi_dhcp'>Ch&#x1EBF; &#x0111;&#x1ED9; IP</label><select id='wifi_dhcp' name='wifi_dhcp'><option value='1' %s>T&#x1EF1; &#x0111;&#x1ED9;ng (DHCP)</option><option value='0' %s>Th&#x1EE7; c&#x00F4;ng (IP t&#x0129;nh)</option></select></div><div id='static' class='%s'><div class='grid'><div class='field'><label for='wifi_ip'>&#x0110;&#x1ECB;a ch&#x1EC9; IP</label><input id='wifi_ip' name='wifi_ip' value='%s' placeholder='192.168.1.20'></div><div class='field'><label for='wifi_netmask'>Netmask</label><input id='wifi_netmask' name='wifi_netmask' value='%s' placeholder='255.255.255.0'></div></div><div class='grid'><div class='field'><label for='wifi_gateway'>Gateway</label><input id='wifi_gateway' name='wifi_gateway' value='%s' placeholder='192.168.1.1'></div><div class='field'><label for='wifi_dns'>DNS</label><input id='wifi_dns' name='wifi_dns' value='%s' placeholder='192.168.1.1'></div></div></div></section>"
             "<section class='card'><h2><span class='step'>03</span>M&#x00E1;y ch&#x1EE7; MQTT</h2><p>C&#x00E0;i &#x0111;&#x1ECB;a ch&#x1EC9; broker m&#x00E0; firmware Callbox s&#x1EED; d&#x1EE5;ng.</p><div class='grid'><div class='field'><label for='mqtt_broker'>Broker / IP</label><input id='mqtt_broker' name='mqtt_broker' value='%s' maxlength='63' required></div><div class='field'><label for='mqtt_port'>C&#x1ED5;ng (Port)</label><input id='mqtt_port' name='mqtt_port' type='number' value='%u' min='1' max='65535' required></div></div><div class='field'><label for='mqtt_user'>T&#x00E0;i kho&#x1EA3;n</label><input id='mqtt_user' name='mqtt_user' value='%s' maxlength='31'></div><div class='field'><label for='mqtt_pass'>M&#x1EAD;t kh&#x1EA9;u</label><input id='mqtt_pass' name='mqtt_pass' type='password' maxlength='63' placeholder='&#x110;&#x1EC3; tr&#x1ED1;ng &#x0111;&#x1EC3; gi&#x1EEF; m&#x1EAD;t kh&#x1EA9;u c&#x169;'></div></section></div>"
             "<div class='save-row'><button class='btn primary' id='save' type='submit'>L&#x01B0;u c&#x1EA5;u h&#xEC;nh</button><button class='btn ghost' id='finish' type='button'>K&#x1EBF;t th&#x00FA;c c&#x00E0;i &#x0111;&#x1EB7;t</button></div><p id='msg' class='message' role='status' aria-live='polite'></p><p class='hint'>C&#x00E0;i &#x0111;&#x1EB7;t WiFi &#x0111;&#x01B0;&#x1EE3;c &#x00E1;p d&#x1EE5;ng m&#x00E0; kh&#x00F4;ng kh&#x1EDF;i &#x0111;&#x1ED9;ng l&#x1EA1;i. H&#x00E3;y gi&#x1EEF; trang m&#x1EDF; &#x0111;&#x1EBF;n khi c&#x00F3; th&#x00F4;ng b&#x00E1;o th&#x00E0;nh c&#x00F4;ng.</p></form>"
             "<script>const f=document.getElementById('f'),m=document.getElementById('msg'),dhcp=document.getElementById('wifi_dhcp'),st=document.getElementById('static'),save=document.getElementById('save');function sync(){st.classList.toggle('hidden',dhcp.value==='1')}dhcp.onchange=sync;sync();async function ping(){fetch('/api/session/ping',{method:'POST'}).catch(()=>{})}fetch('/api/session/open',{method:'POST'}).catch(()=>{});setInterval(ping,5000);document.getElementById('scan').onclick=async()=>{let b=document.getElementById('scan'),t=document.getElementById('scanmsg');b.disabled=true;t.textContent='Dang quet...';try{let a=await(await fetch('/api/wifi-scan')).json(),d=document.getElementById('wifi-list');d.innerHTML='';a.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.label=x.ssid+' ('+x.rssi+' dBm)';d.appendChild(o)});t.textContent=a.length+' mang duoc tim thay';}catch(e){t.textContent='Quet that bai';}b.disabled=false};f.onsubmit=async e=>{e.preventDefault();save.disabled=true;m.className='message';m.textContent='Dang luu...';try{let r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))});m.className=r.ok?'message ok':'message err';m.textContent=await r.text()}catch(e){m.className='message err';m.textContent='Luu that bai. Hay kiem tra ket noi AP.'}save.disabled=false};document.getElementById('finish').onclick=async()=>{await fetch('/api/session/finish',{method:'POST'});m.className='message ok';m.textContent='Da ket thuc cai dat. AP van san sang de khoi phuc.'};window.addEventListener('pagehide',()=>{try{navigator.sendBeacon('/api/session/finish','')}catch(e){}});</script></body></html>",
             callbox, ssid, s_config->wifi_dhcp ? "selected" : "", s_config->wifi_dhcp ? "" : "selected",
             s_config->wifi_dhcp ? "hidden" : "", ip, netmask, gateway, dns,
             broker, (unsigned)s_config->mqtt_port, user);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    replace_legacy_ap_ip();

    /* Keep the header data-rich without adding another API or changing form logic. */
    const bool ap_active = wifi_ap_is_active();
    char hero_meta_markup[768];
    int hero_meta_len = snprintf(
        hero_meta_markup, sizeof(hero_meta_markup),
        "<div class='hero-meta' aria-label='Tr&#x1EA1;ng th&#x00E1;i thi&#x1EBF;t b&#x1ECB;'>"
        "<div class='meta-pill'><i class='meta-icon online' aria-hidden='true'></i><span><small>Tr&#x1EA1;ng th&#x00E1;i</small><strong>Online</strong></span></div>"
        "<div class='meta-pill'><i class='meta-icon device' aria-hidden='true'></i><span><small>ID Callbox</small><strong>%s</strong></span></div>"
        "<div class='meta-pill'><i class='meta-icon wifi' aria-hidden='true'></i><span><small>AP</small><strong>%s</strong></span></div>"
        "<div class='meta-pill'><i class='meta-icon firmware' aria-hidden='true'></i><span><small>Firmware</small><strong>ESP32-S3</strong></span></div>"
        "</div>",
         callbox, ap_active ? CALLBOX_AP_IP_ADDR : "T&#x1EAF;t");
    char *header_end = strstr(s_page_html, "</header>");
    size_t page_before_meta = strlen(s_page_html);
    if (header_end && hero_meta_len > 0 &&
        (size_t)hero_meta_len < sizeof(hero_meta_markup) &&
        page_before_meta + (size_t)hero_meta_len + 1 < sizeof(s_page_html)) {
        memmove(header_end + hero_meta_len, header_end, page_before_meta - (size_t)(header_end - s_page_html) + 1);
        memcpy(header_end, hero_meta_markup, (size_t)hero_meta_len);
    }

    /* Monitoring endpoint remains available, but the dashboard is intentionally
     * omitted from the setup page to keep configuration focused. */
#if 0
    static const char io_monitor_markup[] =
        "<style>.monitor{margin:0 0 14px}.monitor-head{display:flex;align-items:center;justify-content:space-between;gap:12px}.monitor-head h2{margin:0}.io-health{display:flex;align-items:center;gap:7px;color:var(--muted);font-size:12px}.io-health i{width:8px;height:8px;border-radius:50%;background:#8792ae}.io-health.ok i{background:#5df2c2;box-shadow:0 0 0 4px rgba(93,242,194,.14)}.io-health.err i{background:var(--danger);box-shadow:0 0 0 4px rgba(255,158,177,.14)}.monitor-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:14px}.io-group{padding:12px;background:rgba(8,13,39,.26);border:1px solid rgba(170,193,255,.18);border-radius:14px}.io-label{color:#c8d4f0;font-size:13px;margin-bottom:9px}.io-chips{display:flex;flex-wrap:wrap;gap:8px}.io-chip{min-width:76px;min-height:44px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;padding:6px 9px;border-radius:11px;background:rgba(79,124,255,.08);border:1px solid rgba(170,193,255,.18);color:#c8d4f0;font-size:12px}.io-chip b{font-size:12px;font-weight:650}.io-chip small{font-size:10px;letter-spacing:.08em;color:#8e9abc}.io-chip .io-dot{width:7px;height:7px;border-radius:50%;background:#8792ae}.io-chip.on{background:rgba(34,211,238,.12);border-color:rgba(34,211,238,.55);color:#ecfeff}.io-chip.on .io-dot{background:var(--cyan);box-shadow:0 0 0 4px rgba(34,211,238,.14)}.io-chip.on small{color:#7ff7ff}.io-chip.red.on{background:rgba(255,117,125,.13);border-color:rgba(255,117,125,.56)}.io-chip.red.on .io-dot{background:#ff7c8d;box-shadow:0 0 0 4px rgba(255,124,141,.14)}.io-chip.yellow.on{background:rgba(250,204,21,.14);border-color:rgba(250,204,21,.58);color:#fff8cf}.io-chip.yellow.on .io-dot{background:#facc15;box-shadow:0 0 0 4px rgba(250,204,21,.14)}.io-chip.green.on{background:rgba(93,242,194,.13);border-color:rgba(93,242,194,.56)}.io-chip.green.on .io-dot{background:#5df2c2;box-shadow:0 0 0 4px rgba(93,242,194,.14)}@media(max-width:760px){.monitor-grid{grid-template-columns:1fr}.monitor-head{align-items:flex-start;flex-direction:column}}</style>"
        "<section class='monitor card' aria-live='polite'><div class='monitor-head'><h2><span class='step'>IO</span>Gi&#x00E1;m s&#x00E1;t thi&#x1EBF;t b&#x1ECB;</h2><span id='io-health' class='io-health'><i></i>&#x0110;ang &#x0111;&#x1ECD;c...</span></div><div class='monitor-grid'><div class='io-group'><div class='io-label'>N&#x00FA;t nh&#x1EA5;n</div><div id='io-buttons' class='io-chips'></div></div><div class='io-group'><div class='io-label'>Th&#x00E1;p &#x0111;&#x00E8;n</div><div id='io-tower' class='io-chips'></div></div><div class='io-group'><div class='io-label'>&#x0110;&#x1EA7;u ra kh&#x00E1;c</div><div id='io-aux' class='io-chips'></div></div></div></section>"
        "<script>(function(){const labels=['N&#x00FA;t 1','N&#x00FA;t 2','H&#x1EE7;y'],towerLabels=['&#x0110;&#x1ECF;','V&#x00E0;ng','Xanh'],auxLabels=['LED 1','LED 2','LED 3','C&#x00F2;i','LED AP'];function chip(label,on,kind){return \"<span class='io-chip \"+(on?'on ':'off ')+(kind||'')+\"><i class='io-dot'></i><b>\"+label+\"</b><small>\"+(on?'ON':'OFF')+\"</small></span>\"}function draw(r){const b=document.getElementById('io-buttons'),t=document.getElementById('io-tower'),a=document.getElementById('io-aux');b.innerHTML=r.buttons.map((v,i)=>chip(labels[i],v)).join('');t.innerHTML=r.tower? [r.tower.red,r.tower.yellow,r.tower.green].map((v,i)=>chip(towerLabels[i],v,['red','yellow','green'][i])).join(''):'';a.innerHTML=(r.button_leds||[]).concat([r.buzzer,r.ap_led]).map((v,i)=>chip(auxLabels[i],v)).join('')}async function refresh(){const h=document.getElementById('io-health');try{const r=await (await fetch('/api/io-status',{cache:'no-store'})).json();draw(r);h.className='io-health ok';h.innerHTML='<i></i>Online'}catch(e){h.className='io-health err';h.innerHTML='<i></i>Kh&#x00F4;ng &#x0111;&#x1ECD;c &#x0111;&#x01B0;&#x1EE3;c'}}refresh();setInterval(refresh,200)})();</script>";
    size_t monitor_len = strlen(io_monitor_markup);
    char *form_start = strstr(s_page_html, "<form id='f'>");
    size_t page_before_monitor = strlen(s_page_html);
    if (form_start && page_before_monitor + monitor_len + 1 < sizeof(s_page_html)) {
        size_t offset = (size_t)(form_start - s_page_html);
        memmove(form_start + monitor_len, form_start, page_before_monitor - offset + 1);
        memcpy(form_start, io_monitor_markup, monitor_len);
    }
#endif

    static const char layout_css[] =
        "<style>"
        ":root{--bg0:#0b1028;--bg1:#141a3a;--glass:rgba(25,33,73,.82);--line:rgba(147,164,255,.20);--line-hover:rgba(104,202,255,.48);--input-bg:rgba(10,16,46,.68);--text:#f7f8ff;--muted:#bec5e1;--muted-2:#7e88aa;--blue:#715cff;--violet:#a657f7;--cyan:#39cdf8;--success:#43d7a3;--warning:#f4c95d;--danger:#ff687d;--orange:#ff9f5a}"
        "html,body{overflow-x:hidden}body{background:radial-gradient(circle at 8% 10%,rgba(166,87,247,.22),transparent 34%),radial-gradient(circle at 92% 20%,rgba(57,205,248,.15),transparent 32%),linear-gradient(135deg,#24133f 0%,#111735 48%,#173557 100%)}main{width:calc(100% - 24px);max-width:none;margin-inline:auto;padding:18px 0 32px}"
        ".hero{display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:center;gap:20px}.hero .brand{grid-column:1}.hero>.status{display:none}.hero-meta{grid-column:2;grid-row:1;display:flex;align-items:center;justify-content:flex-end;gap:10px;position:relative;z-index:1;flex-wrap:wrap}.meta-pill{min-height:50px;display:flex;align-items:center;gap:10px;padding:8px 14px;border:1px solid rgba(147,164,255,.24);border-radius:12px;background:rgba(10,16,46,.42);color:var(--muted);white-space:nowrap}.meta-pill span{display:flex;flex-direction:column;line-height:1.15}.meta-pill small{font-size:11px;color:#9ca9cf}.meta-pill strong{font-size:14px;color:var(--text);font-weight:650}.meta-icon{width:24px;height:24px;flex:none;position:relative;color:var(--cyan);border:1.5px solid currentColor;border-radius:8px}.meta-icon.online{border:0;border-radius:50%;background:rgba(67,215,163,.18)}.meta-icon.online:before{content:'';position:absolute;width:9px;height:9px;left:7px;top:7px;border-radius:50%;background:var(--success);box-shadow:0 0 0 3px rgba(67,215,163,.12)}.meta-icon.device:after{content:'';position:absolute;left:5px;right:5px;bottom:5px;height:2px;background:currentColor;box-shadow:0 -8px 0 -1px currentColor}.meta-icon.wifi{border-radius:50%;border-width:2px;border-left-color:transparent;border-right-color:transparent;transform:rotate(-25deg)}.meta-icon.firmware{transform:rotate(30deg);border-radius:6px}.logo{background:transparent;padding:0;border-radius:0;filter:grayscale(1) invert(1);mix-blend-mode:screen}"
        ".layout{gap:16px}.card{padding:18px;border-color:rgba(147,164,255,.20)}.card:before{opacity:.28}.monitor.card:before{opacity:.66}.field{margin:10px 0}.field input,.field select{min-height:46px;background:var(--input-bg);border-color:var(--line)}.field input::placeholder{color:#a4afd0}.field input:hover,.field select:hover{border-color:var(--line-hover)}.field input:focus-visible,.field select:focus-visible{outline:none;border-color:var(--cyan);box-shadow:0 0 0 3px rgba(57,205,248,.12)}.field input:disabled,.field select:disabled{opacity:.55;cursor:not-allowed}.field input:invalid:not(:placeholder-shown),.field select:invalid{border-color:var(--danger)}.btn{min-height:50px}.btn:disabled{filter:saturate(.7)}.save-row{margin-top:14px}.save-row .btn{min-height:50px}"
        ".monitor{padding:16px}.monitor-grid{grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;align-items:start}.io-group{display:flex;flex-direction:column;min-height:0;padding:12px;background:rgba(31,41,88,.36);border-color:rgba(147,164,255,.20)}.io-label{margin-bottom:10px}.io-chips{display:grid;grid-template-columns:repeat(auto-fit,minmax(90px,1fr));gap:10px;align-items:stretch}.io-chip{width:100%;min-width:0;min-height:82px;padding:9px 10px;border-radius:12px;background:rgba(10,16,46,.42);border:1px solid rgba(147,164,255,.18);color:var(--muted)}.io-chip b{font-size:14px}.io-chip small{font-size:12px;color:var(--muted-2)}.io-chip .io-dot{width:9px;height:9px}.io-chip.on{background:rgba(57,205,248,.12);border-color:rgba(57,205,248,.58);color:#f2fdff}.io-chip.on .io-dot{background:var(--cyan);box-shadow:0 0 0 4px rgba(57,205,248,.14)}.io-chip.on small{color:#bdf6ff}.monitor-grid .io-group:nth-child(1) .io-chip.on{background:rgba(67,215,163,.13);border-color:rgba(67,215,163,.62)}.monitor-grid .io-group:nth-child(1) .io-chip.on .io-dot{background:var(--success);box-shadow:0 0 0 4px rgba(67,215,163,.14)}.monitor-grid .io-group:nth-child(1) .io-chip.on small{color:#9ff5d5}.io-chip.red.on{background:rgba(255,104,125,.13);border-color:rgba(255,104,125,.62)}.io-chip.red.on .io-dot{background:var(--danger);box-shadow:0 0 0 4px rgba(255,104,125,.14)}.io-chip.red.on small{color:#ffb8c3}.io-chip.yellow.on{background:rgba(244,201,93,.14);border-color:rgba(244,201,93,.64);color:#fff8d7}.io-chip.yellow.on .io-dot{background:var(--warning);box-shadow:0 0 0 4px rgba(244,201,93,.14)}.io-chip.yellow.on small{color:#ffe89a}.io-chip.green.on{background:rgba(67,215,163,.13);border-color:rgba(67,215,163,.62)}.io-chip.green.on .io-dot{background:var(--success);box-shadow:0 0 0 4px rgba(67,215,163,.14)}.io-chip.green.on small{color:#9ff5d5}.monitor-grid .io-group:nth-child(3) .io-chip.on{background:rgba(57,205,248,.12);border-color:rgba(57,205,248,.58)}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(4).on{background:rgba(255,159,90,.14);border-color:rgba(255,159,90,.64)}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(4).on .io-dot{background:var(--orange);box-shadow:0 0 0 4px rgba(255,159,90,.14)}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(5).on .io-dot{background:var(--cyan);box-shadow:0 0 0 4px rgba(57,205,248,.14)}"
        "@media(min-width:768px){.layout .card:nth-child(2){display:grid;grid-template-columns:minmax(0,1.1fr) minmax(0,.9fr);column-gap:12px;align-content:start}.layout .card:nth-child(2)>h2,.layout .card:nth-child(2)>p{grid-column:1 / -1}.layout .card:nth-child(2)>.field:first-of-type{grid-column:1 / -1;display:grid;grid-template-columns:minmax(0,1fr) auto;grid-template-areas:'label label' 'ssid scan';column-gap:12px}.layout .card:nth-child(2)>.field:first-of-type label{grid-area:label}.layout .card:nth-child(2)>.field:first-of-type input{grid-area:ssid}.layout .card:nth-child(2)>.field:first-of-type .actions{grid-area:scan;margin-top:0;align-self:end}.layout .card:nth-child(2)>.field:first-of-type .actions button{white-space:nowrap}.layout .card:nth-child(2)>.field:nth-of-type(2){grid-column:1}.layout .card:nth-child(2)>.field:nth-of-type(3){grid-column:2}.layout .card:nth-child(2)>#static{grid-column:1 / -1}}"
        "@media(min-width:1024px){main{width:min(1440px,calc(100% - 48px));padding:20px 0 32px}.layout{grid-template-columns:minmax(0,.9fr) minmax(0,1.1fr);align-items:stretch}.layout .card:nth-child(1){grid-column:1;grid-row:1}.layout .card:nth-child(2){grid-column:2;grid-row:1}.layout .card:nth-child(3){grid-column:1 / -1;grid-row:2}}"
        "@media(min-width:768px) and (max-width:1199px){.layout .card:nth-child(3){display:grid;grid-template-columns:minmax(0,2fr) minmax(0,.9fr);gap:12px}.layout .card:nth-child(3)>h2,.layout .card:nth-child(3)>p{grid-column:1 / -1}.layout .card:nth-child(3)>.grid{display:contents}.layout .card:nth-child(3)>.field,.layout .card:nth-child(3)>.grid>.field{margin:0}}"
        "@media(min-width:1200px){.monitor-grid{grid-template-columns:minmax(0,1fr) minmax(0,1fr) minmax(0,1.35fr)}.monitor-grid .io-group:nth-child(3) .io-chips{grid-template-columns:repeat(5,minmax(0,1fr))}.layout .card:nth-child(3){display:grid;grid-template-columns:minmax(0,2fr) minmax(120px,.7fr) minmax(0,1fr) minmax(0,1.5fr);gap:12px}.layout .card:nth-child(3)>h2,.layout .card:nth-child(3)>p{grid-column:1 / -1}.layout .card:nth-child(3)>.grid{display:contents}.layout .card:nth-child(3)>.field,.layout .card:nth-child(3)>.grid>.field{margin:0}}"
        "@media(max-width:1023px){.layout{grid-template-columns:1fr}.layout .card:nth-child(1),.layout .card:nth-child(2),.layout .card:nth-child(3){grid-column:auto;grid-row:auto}.hero{display:block}.hero-meta{grid-column:auto;grid-row:auto;justify-content:flex-start;margin-top:16px;overflow:visible;max-width:100%;flex-wrap:wrap}.monitor-grid{grid-template-columns:1fr}}"
        "@media(max-width:767px){main{width:calc(100% - 24px);padding:14px 0 28px}.hero{display:block}.hero-meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.meta-pill{min-width:0;min-height:48px;padding:7px 10px}.meta-pill strong{overflow:hidden;text-overflow:ellipsis}.layout .card:nth-child(2){display:block}.layout .card:nth-child(2)>.field:first-of-type{display:block}.layout .card:nth-child(2)>.field:first-of-type .actions{margin-top:8px}.layout .card:nth-child(3){display:block}.layout .card:nth-child(3)>.grid{display:grid}.monitor-grid{grid-template-columns:1fr}.io-chips{grid-template-columns:repeat(auto-fit,minmax(90px,1fr))}}"
        "@media(min-width:1024px){main{width:min(1360px,calc(100% - 48px))}}"
        ".hero p{font-size:14px}.field label{font-size:13px;font-weight:600}.field input,.field select{font-size:14px}.btn{font-size:14px;font-weight:650}.card:before{left:18px;right:auto;width:56px;opacity:.32}.monitor.card:before{width:64px;opacity:.68}.io-chip{transition:background-color .18s,border-color .18s,color .18s,box-shadow .18s}.io-health{font-size:0}.io-health:after{content:'I/O Live';font-size:12px}.io-health.err:after{content:'I/O error';font-size:12px}"
        "@media(max-width:767px){.save-row{grid-template-columns:1fr}}"
        ":root{--background:#0d1525;--surface:#172236;--surface-strong:#202d43;--surface-soft:#111a2c;--border:#3b4b64;--border-soft:#2d3b52;--text:#f8fafc;--muted:#a9b7ca;--accent:#34d399;--accent-strong:#047857;--accent-ink:#03261d;--warning:#fbbf24;--danger:#f87171;--orange:#fb923c;--shadow:0 18px 45px rgba(2,6,23,.24)}html{background:var(--background)}body{background:radial-gradient(circle at 78% 8%,#153352 0,transparent 35%),var(--background);color:var(--text);font:16px/1.5 system-ui,-apple-system,Segoe UI,Arial,sans-serif}main{width:min(1360px,calc(100% - 48px));max-width:none;margin-inline:auto;padding:22px 0 38px}"
        ".hero{background:var(--surface-strong);border:1px solid var(--border);border-radius:11px;padding:18px 20px;box-shadow:var(--shadow);backdrop-filter:none;-webkit-backdrop-filter:none}.hero:after{display:none}.hero h1{font-size:26px}.hero p{color:var(--muted);font-size:14px}.logo{background:transparent;border-radius:8px;padding:0;filter:none;mix-blend-mode:normal}.hero-meta{gap:9px}.meta-pill{min-height:50px;padding:8px 14px;border:1px solid var(--border);border-radius:999px;background:var(--surface-soft);color:var(--muted);box-shadow:none}.meta-pill small{color:var(--muted);font-size:11px}.meta-pill strong{color:var(--text);font-size:14px;font-weight:650}.meta-icon{color:var(--accent);border-color:currentColor}.meta-icon.online{background:rgba(52,211,153,.12)}"
        ".layout{gap:14px}.card{padding:18px;background:var(--surface);border:1px solid var(--border-soft);border-radius:11px;box-shadow:var(--shadow);backdrop-filter:none;-webkit-backdrop-filter:none}.card:before,.monitor.card:before{display:none}.card h2{font-size:17px}.step{width:28px;height:28px;border:1px solid rgba(52,211,153,.55);border-radius:8px;background:rgba(52,211,153,.12);color:var(--accent);box-shadow:none}.field label{color:var(--muted);font-size:13px;font-weight:600}.field input,.field select{min-height:46px;padding:9px 12px;border:1px solid var(--border);border-radius:8px;background:var(--surface-soft);color:var(--text);font-size:14px}.field input::placeholder{color:#8190a6}.field input:hover,.field select:hover{border-color:#64748b}.field input:focus-visible,.field select:focus-visible{outline:3px solid rgba(52,211,153,.18);border-color:var(--accent);box-shadow:0 0 0 1px var(--accent)}"
        ".btn{min-height:50px;border:1px solid var(--border);border-radius:8px;background:var(--surface-soft);color:var(--text);font-size:14px;font-weight:650;transition:background-color .18s,border-color .18s,color .18s,transform .18s}.btn:hover:not(:disabled){border-color:var(--accent);color:var(--accent)}.btn:active:not(:disabled){transform:translateY(1px)}.primary{border-color:var(--accent-strong);background:var(--accent-strong);color:#ecfdf5;box-shadow:none}.primary:hover:not(:disabled){background:#065f46;border-color:var(--accent);color:#fff}.secondary,.ghost{border-color:var(--border);background:transparent;color:var(--text)}.secondary:hover:not(:disabled),.ghost:hover:not(:disabled){background:rgba(52,211,153,.08);border-color:var(--accent);color:var(--accent)}.btn:focus-visible{outline:3px solid rgba(52,211,153,.28);outline-offset:2px}.btn:disabled{opacity:.62;cursor:wait}"
        ".monitor{background:var(--surface);border-color:var(--border)}.monitor-head h2{font-size:17px}.io-health{color:var(--muted)}.io-health.ok i{background:var(--accent);box-shadow:0 0 0 3px rgba(52,211,153,.12)}.io-health.err i{background:var(--danger);box-shadow:0 0 0 3px rgba(248,113,113,.12)}.io-group{padding:12px;background:var(--surface-soft);border:1px solid var(--border-soft);border-radius:9px}.io-label{color:var(--muted);font-size:13px;font-weight:650}.io-chip{min-height:78px;padding:10px 12px;border:1px solid var(--border);border-radius:9px;background:var(--surface);color:var(--muted);box-shadow:none}.io-chip b{color:inherit;font-size:14px;font-weight:650}.io-chip small{color:#8190a6;font-size:12px}.io-chip .io-dot{background:#64748b;box-shadow:none}.io-chip.on{background:rgba(52,211,153,.12);border-color:var(--accent);color:var(--text);box-shadow:none}.io-chip.on .io-dot{background:var(--accent);box-shadow:0 0 0 3px rgba(52,211,153,.12)}.io-chip.on small{color:#a7f3d0}.monitor-grid .io-group:nth-child(1) .io-chip.on{background:rgba(52,211,153,.12);border-color:var(--accent)}.io-chip.red.on{background:rgba(248,113,113,.12);border-color:var(--danger)}.io-chip.red.on .io-dot{background:var(--danger);box-shadow:0 0 0 3px rgba(248,113,113,.12)}.io-chip.red.on small{color:#fecaca}.io-chip.yellow.on{background:rgba(251,191,36,.12);border-color:var(--warning);color:var(--text)}.io-chip.yellow.on .io-dot{background:var(--warning);box-shadow:0 0 0 3px rgba(251,191,36,.12)}.io-chip.yellow.on small{color:#fde68a}.io-chip.green.on{background:rgba(52,211,153,.12);border-color:var(--accent)}.io-chip.green.on .io-dot{background:var(--accent);box-shadow:0 0 0 3px rgba(52,211,153,.12)}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(4).on{background:rgba(251,146,60,.12);border-color:var(--orange)}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(4).on .io-dot{background:var(--orange);box-shadow:0 0 0 3px rgba(251,146,60,.12)}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(5).on{background:rgba(52,211,153,.12);border-color:var(--accent)}"
        "@media(max-width:767px){main{width:calc(100% - 24px);padding:16px 0 30px}.hero{padding:16px}.card{padding:16px}.meta-pill{min-height:48px;padding:8px 11px}.meta-pill strong{font-size:13px}}"
        /* The I/O tiles are a grid, not a wrapping flex row.  The previous
         * flex rule made every tile 100%% wide and left the monitor card
         * unnecessarily tall. */
        ".monitor-grid .io-chips{display:grid;grid-template-columns:repeat(auto-fit,minmax(90px,1fr));gap:10px;align-items:stretch}.monitor-grid .io-chip{width:auto;min-width:0}.monitor-grid .io-group:nth-child(1) .io-chips,.monitor-grid .io-group:nth-child(2) .io-chips{grid-template-columns:repeat(3,minmax(0,1fr))}"
        "@media(min-width:768px) and (max-width:1199px){.monitor-grid .io-group:nth-child(3) .io-chips{grid-template-columns:repeat(3,minmax(0,1fr))}}"
        "@media(min-width:1200px){.monitor-grid .io-group:nth-child(3) .io-chips{grid-template-columns:repeat(5,minmax(0,1fr))}}"
        ".monitor-grid .io-chip{position:relative}.monitor-grid .io-chip::before,.monitor-grid .io-chip::after{content:'';display:block;flex:none;box-sizing:border-box}.monitor-grid .io-group:nth-child(1) .io-chip::before{width:22px;height:22px;border:2px solid #8ea0c2;border-radius:50%;box-shadow:inset 0 0 0 3px var(--surface)}.monitor-grid .io-group:nth-child(1) .io-chip::after{width:6px;height:6px;margin-top:-14px;border-radius:50%;background:#8ea0c2}.monitor-grid .io-group:nth-child(1) .io-chip.on::before{border-color:var(--accent)}.monitor-grid .io-group:nth-child(1) .io-chip.on::after{background:var(--accent)}"
        ".monitor-grid .io-group:nth-child(2) .io-chip::before,.monitor-grid .io-group:nth-child(3) .io-chip::before{width:18px;height:20px;border:2px solid #8ea0c2;border-radius:50% 50% 44% 44%}.monitor-grid .io-group:nth-child(2) .io-chip::after,.monitor-grid .io-group:nth-child(3) .io-chip::after{width:11px;height:4px;margin-top:-1px;border-top:2px solid #8ea0c2;border-bottom:2px solid #8ea0c2}.monitor-grid .io-group:nth-child(2) .io-chip.red::before,.monitor-grid .io-group:nth-child(2) .io-chip.red::after{border-color:#ff7183}.monitor-grid .io-group:nth-child(2) .io-chip.yellow::before,.monitor-grid .io-group:nth-child(2) .io-chip.yellow::after{border-color:#f4c95d}.monitor-grid .io-group:nth-child(2) .io-chip.green::before,.monitor-grid .io-group:nth-child(2) .io-chip.green::after{border-color:#43d7a3}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(-n+3)::before,.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(-n+3)::after{border-color:#7ca7ff}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(4)::before,.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(4)::after{border-color:#ff9f5a}.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(5)::before,.monitor-grid .io-group:nth-child(3) .io-chip:nth-child(5)::after{border-color:var(--cyan)}"
        ".monitor-grid .io-chip{position:relative;justify-content:flex-start;gap:2px;padding:40px 10px 10px}.monitor-grid .io-chip::before,.monitor-grid .io-chip::after{position:absolute;display:block;z-index:0;transform:translateX(-50%);margin:0}.monitor-grid .io-chip::before{top:8px;left:50%}.monitor-grid .io-chip::after{top:31px;left:50%}.monitor-grid .io-chip b,.monitor-grid .io-chip small{position:relative;z-index:1}.monitor-grid .io-chip .io-dot{position:absolute;right:8px;bottom:8px;width:7px;height:7px;margin:0}.monitor-grid .io-group:nth-child(1) .io-chip::after{top:30px}.monitor-grid .io-group:nth-child(1) .io-chip::before{box-shadow:inset 0 0 0 3px var(--surface)}"
         "@media(min-width:1024px){.layout .card:nth-child(1){align-self:stretch;display:flex;flex-direction:column}.layout .card:nth-child(1)>.field{margin-top:auto;margin-bottom:auto}}"
         ":root{--glow-line:rgba(52,211,153,.78);--glow-medium:rgba(52,211,153,.28);--glow-soft:rgba(52,211,153,.13);--glow-shadow:0 0 0 1px var(--glow-line),0 0 18px var(--glow-medium),0 0 38px var(--glow-soft);--glow-shadow-soft:0 0 16px var(--glow-soft)}.monitor-grid .io-chip.on{box-shadow:var(--glow-shadow)}.io-health.ok{color:var(--accent);text-shadow:0 0 12px var(--glow-medium)}.io-health.ok i{box-shadow:0 0 0 4px var(--glow-soft),0 0 14px var(--glow-medium)}.meta-pill:first-child{border-color:var(--glow-line);box-shadow:var(--glow-shadow-soft)}"
         /* Desktop uses a two-column shell: setup cards stay in a compact
          * stack on the left, while live I/O monitoring occupies the right.
          * The form/monitor DOM order is intentionally unchanged so all
          * existing IDs, handlers, and WebSocket bindings keep working. */
         "@media(min-width:1200px){main{display:grid;grid-template-columns:minmax(0,.88fr) minmax(0,1.12fr);column-gap:16px;align-items:start}main>.hero{grid-column:1 / -1;grid-row:1}main>.monitor{grid-column:2;grid-row:2;margin:0;min-width:0}main>form{grid-column:1;grid-row:2;min-width:0;margin:0}main>form .layout{display:flex;flex-direction:column;gap:14px}main>form .layout>.card{width:100%;min-width:0;margin:0}main>form .layout>.card:nth-child(1){display:block;align-self:auto}main>form .layout>.card:nth-child(2){display:block}main>form .layout>.card:nth-child(3){display:grid;grid-template-columns:minmax(0,2fr) minmax(95px,.7fr) minmax(0,1fr) minmax(0,1.5fr);gap:10px;align-items:end}main>form .layout>.card:nth-child(3)>h2,main>form .layout>.card:nth-child(3)>p{grid-column:1 / -1}main>form .layout>.card:nth-child(3)>.grid{display:contents}main>form .layout>.card:nth-child(3)>.field,main>form .layout>.card:nth-child(3)>.grid>.field{margin:0}main>form .save-row{margin-top:14px}.monitor-grid{grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:12px}.monitor-grid .io-group:nth-child(3){grid-column:1 / -1}.monitor-grid .io-group:nth-child(3) .io-chips{grid-template-columns:repeat(5,minmax(0,1fr))}}"
         "@media(min-width:1200px){main>.monitor{align-self:stretch;display:flex;flex-direction:column}.monitor-grid{flex:1;grid-template-rows:minmax(190px,.9fr) minmax(210px,1.1fr);align-content:stretch}.monitor-grid .io-group{min-height:0}.monitor-grid .io-chips{flex:1;align-content:center}.monitor-grid .io-chip{min-height:132px}}"
         "@media(min-width:1024px){main{display:block}main>form{display:block;min-width:0;margin:0}.layout,main>form .layout{display:grid;grid-template-columns:minmax(0,.72fr) minmax(0,1.28fr);gap:14px;align-items:start}main>form .layout>.card{width:auto;min-width:0;margin:0}main>form .layout>.card:nth-child(1){display:block;grid-column:1;grid-row:1;align-self:start}main>form .layout>.card:nth-child(1)>.field{margin:10px 0}main>form .layout>.card:nth-child(2){display:grid;grid-column:2;grid-row:1}main>form .layout>.card:nth-child(3){display:grid;grid-template-columns:minmax(0,2fr) minmax(120px,.7fr) minmax(0,1fr) minmax(0,1.5fr);grid-column:1 / -1;grid-row:2;gap:12px;align-items:end}main>form .layout>.card:nth-child(3)>h2,main>form .layout>.card:nth-child(3)>p{grid-column:1 / -1}main>form .layout>.card:nth-child(3)>.grid{display:contents}main>form .layout>.card:nth-child(3)>.field,main>form .layout>.card:nth-child(3)>.grid>.field{margin:0}}"
         "@media(min-width:1024px){.layout,main>form .layout{grid-template-columns:1fr}main>form .layout>.card:nth-child(1){grid-column:1;grid-row:1}main>form .layout>.card:nth-child(2){grid-column:1;grid-row:2}main>form .layout>.card:nth-child(3){grid-column:1;grid-row:3}}"
         "</style>";
    /* Inject the final layout stylesheet inside <head>.  Appending it after
     * </html> makes the browser paint the fallback layout first, producing a
     * visible one-frame flash on every refresh. */
    size_t page_len = strlen(s_page_html);
    size_t css_len = strlen(layout_css);
    char *head_end = strstr(s_page_html, "</head>");
    if (head_end && page_len + css_len + 1 < sizeof(s_page_html)) {
        size_t head_offset = (size_t)(head_end - s_page_html);
        memmove(head_end + css_len, head_end, page_len - head_offset + 1);
        memcpy(head_end, layout_css, css_len);
    }

    /* Bump the asset URL so a browser cannot reuse the previous logo from
     * its long-lived image cache after a firmware update. */
    const char old_logo_ref[] = "src='/logo.jpg'";
    const char new_logo_ref[] = "src='/logo.jpg?v=4'";
    char *logo_ref = strstr(s_page_html, old_logo_ref);
    if (logo_ref && strlen(new_logo_ref) > strlen(old_logo_ref)) {
        size_t old_len = strlen(old_logo_ref);
        size_t new_len = strlen(new_logo_ref);
        size_t current_len = strlen(s_page_html);
        if (current_len + (new_len - old_len) + 1 < sizeof(s_page_html)) {
            memmove(logo_ref + new_len, logo_ref + old_len,
                    current_len - (size_t)(logo_ref - s_page_html) - old_len + 1);
            memcpy(logo_ref, new_logo_ref, new_len);
        }
    }
    return httpd_resp_send(req, s_page_html, HTTPD_RESP_USE_STRLEN);
}

/* The production page deliberately stays in one template.  Older revisions
 * layered several CSS overrides after rendering the initial page, which made
 * the layout difficult to maintain and could briefly show a stale layout.
 * Trang cấu hình CHÍNH được dùng từ root_handler; giữ mọi thứ trong một
 * template để dễ bảo trì và không có hiệu ứng layout cũ. */
static esp_err_t portal_page_handler_modern(httpd_req_t *req)
{
    char callbox[64], device_name[96], ssid[96], broker[128], user[64];
    char ip[32], netmask[32], gateway[32], dns[32];
    html_escape(s_config->callbox_id, callbox, sizeof(callbox));
    html_escape(s_config->wifi_ssid, ssid, sizeof(ssid));
    html_escape(s_config->mqtt_broker, broker, sizeof(broker));
    html_escape(s_config->mqtt_user, user, sizeof(user));
    html_escape(s_config->wifi_ip, ip, sizeof(ip));
    html_escape(s_config->wifi_netmask, netmask, sizeof(netmask));
    html_escape(s_config->wifi_gateway, gateway, sizeof(gateway));
    html_escape(s_config->wifi_dns, dns, sizeof(dns));
    format_device_name(s_config->callbox_id, device_name, sizeof(device_name));

    snprintf(s_page_html, sizeof(s_page_html),
        "<!doctype html><html lang='vi'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Cai dat Callbox</title><style>"
        ":root{--bg:#0d1525;--surface:#172236;--surface2:#111a2c;--line:#3b4b64;--line2:#2d3b52;--text:#f8fafc;--muted:#a9b7ca;--green:#34d399;--green2:#047857;--cyan:#39cdf8;--red:#f87171;--shadow:0 16px 40px rgba(2,6,23,.22)}*{box-sizing:border-box}html{background:var(--bg)}body{margin:0;background:radial-gradient(circle at 78%% 8%%,#153352 0,transparent 35%%),var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,Segoe UI,Arial,sans-serif}main{width:min(1440px,calc(100%% - 48px));margin-inline:auto;padding:22px 0 38px}.hero,.card,.live{background:var(--surface);border:1px solid var(--line2);border-radius:12px;box-shadow:var(--shadow)}.hero{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:18px 20px}.brand{display:flex;align-items:center;gap:15px;min-width:0}.logo{width:min(178px,38vw);height:42px;object-fit:contain}.eyebrow{font-size:11px;letter-spacing:.15em;color:#92a8e8}.hero h1{margin:2px 0;font-size:26px;line-height:1.15}.hero p{margin:4px 0 0;color:var(--muted)}.device-label{color:var(--muted);font-size:12px}.device-label b{color:var(--green);font-weight:700}.header-status{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:8px}.pill{display:grid;grid-template-columns:auto 1fr;gap:2px 8px;align-items:center;min-height:50px;padding:8px 13px;border:1px solid var(--line);border-radius:11px;background:var(--surface2);white-space:nowrap}.pill i{grid-row:1/3;width:9px;height:9px;border-radius:50%%;background:#64748b}.pill.online i{background:var(--green);box-shadow:0 0 0 4px rgba(52,211,153,.12)}.pill.identity i{background:var(--cyan);box-shadow:0 0 0 4px rgba(57,205,248,.12)}.pill.off i{background:var(--red);box-shadow:0 0 0 4px rgba(248,113,113,.12)}.pill small{color:var(--muted);font-size:11px}.pill b{font-size:13px}.live{display:grid;grid-template-columns:repeat(auto-fit,minmax(142px,1fr));gap:0;margin-top:14px;padding:0}.live-item{min-width:0;padding:12px 14px;border-right:1px solid var(--line2)}.live-item:last-child{border-right:0}.live-item small{display:block;color:var(--muted);font-size:11px}.live-item b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:14px}.ok{color:var(--green)}.bad{color:var(--red)}.layout{display:grid;grid-template-columns:minmax(0,.9fr) minmax(0,1.1fr);gap:14px;margin-top:14px}.card{padding:17px}.mqtt{grid-column:1/-1}.card h2{display:flex;align-items:center;gap:9px;margin:0 0 13px;font-size:17px}.step{display:grid;place-items:center;width:28px;height:28px;border:1px solid rgba(52,211,153,.6);border-radius:8px;background:rgba(52,211,153,.1);color:var(--green);font-size:12px}.field{margin:11px 0}.field label{display:block;margin-bottom:6px;color:var(--muted);font-size:13px;font-weight:650}.field input,.field select{width:100%%;min-height:46px;border:1px solid var(--line);border-radius:9px;padding:10px 12px;background:var(--surface2);color:var(--text);font:inherit}.field input::placeholder{color:#8190a6}.field input:hover,.field select:hover{border-color:#64748b}.field input:focus-visible,.field select:focus-visible,.btn:focus-visible{outline:3px solid rgba(57,205,248,.16);outline-offset:2px;border-color:var(--cyan)}.wifi-top{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:12px;align-items:end}.wifi-bottom{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(180px,.9fr);gap:12px}.static-grid{display:grid;grid-template-columns:1fr 150px;gap:12px}.btn{min-height:46px;border:1px solid var(--line);border-radius:9px;padding:10px 15px;background:transparent;color:var(--text);font:650 14px inherit;cursor:pointer;transition:border-color .18s,background .18s,color .18s,transform .18s}.btn:hover:not(:disabled){border-color:var(--green);background:rgba(52,211,153,.08);color:var(--green)}.btn:active:not(:disabled){transform:translateY(1px)}.btn:disabled{opacity:.6;cursor:wait}.primary{min-height:50px;border-color:var(--green2);background:var(--green2);color:#ecfdf5}.primary:hover:not(:disabled){background:#065f46;color:#fff}.scan-note,.message{min-height:20px;margin:8px 0 0;color:var(--muted);font-size:13px}.message.ok{color:var(--green)}.message.err{color:var(--red)}.wifi-results,.profiles{display:grid;gap:7px;margin-top:10px}.wifi-row,.profile-row{display:flex;align-items:center;justify-content:space-between;gap:10px;min-height:44px;padding:7px 10px;border:1px solid var(--line2);border-radius:8px;background:rgba(17,26,44,.65)}.wifi-row button{min-width:0;flex:1;border:0;background:transparent;color:var(--text);font:650 13px inherit;text-align:left;cursor:pointer}.wifi-row button:hover{color:var(--cyan)}.wifi-rssi{color:var(--muted);font-size:12px;white-space:nowrap}.profile-row span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.profile-row small{color:var(--green);margin-left:6px}.delete{min-height:34px;padding:5px 10px;color:#fca5a5;border-color:rgba(248,113,113,.4)}.delete:hover:not(:disabled){color:#fecaca;border-color:var(--red);background:rgba(248,113,113,.08)}.topic-grid{display:grid;grid-template-columns:1.2fr repeat(3,1fr);gap:10px;margin-top:14px;padding-top:14px;border-top:1px solid var(--line2)}.topic-grid label{display:block;margin-bottom:6px;color:var(--muted);font-size:12px;font-weight:650}.readout{display:block;min-height:42px;overflow:auto;padding:10px;border:1px solid var(--line2);border-radius:8px;background:var(--surface2);color:#c7d2fe;font:12px ui-monospace,SFMono-Regular,Consolas,monospace;white-space:nowrap}.save-row{margin-top:14px}.save-row .btn{width:100%%}.help{margin:8px 0 0;color:var(--muted);font-size:12px}.hidden{display:none!important}@media(max-width:1023px){main{width:calc(100%% - 40px)}.layout{grid-template-columns:1fr}.mqtt{grid-column:auto}.hero{align-items:flex-start;flex-direction:column}.header-status{justify-content:flex-start}}@media(max-width:767px){main{width:calc(100%% - 24px);padding:14px 0 28px}.hero{padding:16px}.brand{align-items:flex-start}.logo{width:132px;height:35px}.hero h1{font-size:22px}.header-status{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));width:100%%}.pill{min-width:0;padding:8px 10px}.pill b{overflow:hidden;text-overflow:ellipsis}.live{grid-template-columns:repeat(2,minmax(0,1fr))}.live-item:nth-child(2n){border-right:0}.wifi-top,.wifi-bottom,.static-grid,.topic-grid{grid-template-columns:1fr}.wifi-top .btn{width:100%%}.card{padding:16px}}@media(prefers-reduced-motion:reduce){*{transition:none!important}}</style></head><body><main>"
        "<header class='hero'><div class='brand'><img class='logo' src='/logo.jpg?v=5' alt='AUBOT'><div><div class='eyebrow'>AUBOT · CALLBOX</div><h1>C&#xE0;i &#x0111;&#x1EB7;t Callbox</h1><p>C&#x1EA5;u h&#xEC;nh thi&#x1EBF;t b&#x1ECB; cho v&#x1EAD;n h&#xE0;nh nh&#xE0; m&#xE1;y</p><div class='device-label'>T&#xEA;n thi&#x1EBF;t b&#x1ECB;: <b id='device-name'>%s</b></div></div></div><div class='header-status'><div class='pill online'><i></i><small>Tr&#x1EA1;ng th&#xE1;i</small><b id='head-sta'>&#x110;ang ki&#x1EC3;m tra</b></div><div class='pill identity'><i></i><small>Callbox ID</small><b id='head-id'>%s</b></div><div class='pill ap-pill'><i></i><small>AP c&#x1EA5;u h&#xEC;nh</small><b id='head-ap'>...</b></div></div></header>"
        "<section class='live' aria-live='polite'><div class='live-item'><small>STA / SSID</small><b id='sta-ssid'>&#x110;ang ki&#x1EC3;m tra...</b></div><div class='live-item'><small>&#x110;&#x1ECB;a ch&#x1EC9; IP</small><b id='sta-ip'>--</b></div><div class='live-item'><small>RSSI</small><b id='sta-rssi'>--</b></div><div class='live-item'><small>Gateway</small><b id='sta-gateway'>--</b></div><div class='live-item'><small>IP m&#x1EB7;c &#x0111;&#x1ECB;nh AP</small><b id='ap-default'>192.168.65.204</b></div><div class='live-item'><small>MQTT</small><b id='mqtt-state'>&#x110;ang ki&#x1EC3;m tra...</b></div></section>"
        "<form id='f'><div class='layout'><section class='card'><h2><span class='step'>01</span>Nh&#x1EAD;n d&#x1EA1;ng thi&#x1EBF;t b&#x1ECB;</h2><div class='field'><label for='callbox_id'>S&#x1ED1; ID Callbox</label><input id='callbox_id' name='callbox_id' value='%s' maxlength='15' inputmode='numeric' pattern='[0-9]*' autocomplete='off' required></div><p class='help'>T&#xEA;n MQTT/thi&#x1EBF;t b&#x1ECB; t&#x1EF1; sinh: AUBOT-Callbox-&lt;ID&gt;.</p></section>"
        "<section class='card'><h2><span class='step'>02</span>WiFi nh&#xE0; m&#xE1;y</h2><div class='wifi-top'><div class='field'><label for='ssid'>SSID m&#x1EA1;ng</label><input id='ssid' name='wifi_ssid' value='%s' maxlength='32' autocomplete='off'></div><div class='field wifi-scan-field'><span class='field-label-spacer' aria-hidden='true'>SSID m&#x1EA1;ng</span><button class='btn' type='button' id='scan'>Qu&#xE9;t m&#x1EA1;ng WiFi</button></div></div><p id='scanmsg' class='scan-note'></p><div id='wifi-results' class='wifi-results' aria-live='polite'></div><div class='wifi-bottom'><div class='field'><label for='wifi_pass'>M&#x1EAD;t kh&#x1EA9;u</label><input id='wifi_pass' type='password' name='wifi_pass' maxlength='63' autocomplete='new-password' placeholder='&#x110;&#x1EC3; tr&#x1ED1;ng &#x0111;&#x1EC3; gi&#x1EEF; m&#x1EAD;t kh&#x1EA9;u c&#x169;'></div><div class='field'><label for='wifi_dhcp'>Ch&#x1EBF; &#x0111;&#x1ED9; IP</label><select id='wifi_dhcp' name='wifi_dhcp'><option value='1' %s>T&#x1EF1; &#x0111;&#x1ED9;ng (DHCP)</option><option value='0' %s>Th&#x1EE7; c&#xF4;ng (IP t&#x0129;nh)</option></select></div></div><div id='static' class='%s'><div class='static-grid'><div class='field'><label for='wifi_ip'>&#x110;&#x1ECB;a ch&#x1EC9; IP</label><input id='wifi_ip' name='wifi_ip' value='%s' placeholder='192.168.1.20'></div><div class='field'><label for='wifi_netmask'>Netmask</label><input id='wifi_netmask' name='wifi_netmask' value='%s'></div></div><div class='static-grid'><div class='field'><label for='wifi_gateway'>Gateway</label><input id='wifi_gateway' name='wifi_gateway' value='%s'></div><div class='field'><label for='wifi_dns'>DNS</label><input id='wifi_dns' name='wifi_dns' value='%s'></div></div></div><div class='field'><label>M&#x1EA1;ng WiFi &#x0111;&#xE3; nh&#x1EDB;</label><div id='profiles' class='profiles'></div></div></section>"
        "<section class='card mqtt'><h2><span class='step'>03</span>M&#xE1;y ch&#x1EE7; MQTT</h2><div class='topic-grid'><div class='field'><label for='mqtt_broker'>Broker / IP</label><input id='mqtt_broker' name='mqtt_broker' value='%s' maxlength='63' required></div><div class='field'><label for='mqtt_port'>C&#x1ED5;ng</label><input id='mqtt_port' name='mqtt_port' type='number' value='%u' min='1' max='65535' required></div><div class='field'><label for='mqtt_user'>T&#xE0;i kho&#x1EA3;n</label><input id='mqtt_user' name='mqtt_user' value='%s' maxlength='31'></div><div class='field'><label for='mqtt_pass'>M&#x1EAD;t kh&#x1EA9;u</label><input id='mqtt_pass' name='mqtt_pass' type='password' maxlength='63' autocomplete='new-password' placeholder='&#x110;&#x1EC3; tr&#x1ED1;ng &#x0111;&#x1EC3; gi&#x1EEF; m&#x1EAD;t kh&#x1EA9;u c&#x169;'></div></div><div class='topic-grid'><div><label>Client ID th&#x1EF1;c t&#x1EBF;</label><code id='mqtt-client' class='readout'>--</code></div><div><label>Topic l&#x1EC7;nh</label><code id='topic-cmd' class='readout'>--</code></div><div><label>Topic s&#x1EF1; ki&#x1EC7;n</label><code id='topic-event' class='readout'>--</code></div><div><label>Topic tr&#x1EA1;ng th&#xE1;i</label><code id='topic-status' class='readout'>--</code></div></div></section></div><div class='save-row'><button class='btn primary' id='save' type='submit'>L&#x01B0;u v&#xE0; &#xE1;p d&#x1EE5;ng c&#x1EA5;u h&#xEC;nh</button></div><p id='msg' class='message' role='status' aria-live='polite'></p><p class='help'>Sau khi l&#x01B0;u, Callbox th&#x1EED; k&#x1EBF;t n&#x1ED1;i theo c&#x1EA5;u h&#xEC;nh m&#x1EDB;i. &#x110;&#xF3;ng trang khi &#x0111;&#xE3; ho&#xE0;n t&#x1EA5;t c&#xE0;i &#x0111;&#x1EB7;t.</p></form>"
        "<script>(function(){const f=document.getElementById('f'),m=document.getElementById('msg'),dhcp=document.getElementById('wifi_dhcp'),st=document.getElementById('static'),save=document.getElementById('save'),id=document.getElementById('callbox_id');function sync(){st.classList.toggle('hidden',dhcp.value==='1')}dhcp.addEventListener('change',sync);sync();id.addEventListener('input',()=>{id.value=id.value.replace(/\\D/g,'');document.getElementById('device-name').textContent='AUBOT-Callbox-'+(id.value||'...');document.getElementById('head-id').textContent=id.value||'...'});async function api(u,o){const r=await fetch(u,Object.assign({cache:'no-store'},o||{}));if(!r.ok)throw Error(await r.text());return r.json()}function put(k,v,ok){const e=document.getElementById(k);e.textContent=v;e.className=ok===undefined?'':ok?'ok':'bad'}async function status(){try{const x=await api('/api/status');put('head-sta',x.sta?'STA &#x0111;&#xE3; k&#x1EBF;t n&#x1ED1;i':'STA ch&#x01B0;a k&#x1EBF;t n&#x1ED1;i',!!x.sta);put('head-ap',x.ap?'&#x0110;ang b&#x1EAD;t':'T&#x1EAF;t',!!x.ap);put('sta-ssid',x.sta?x.ssid:'Ch&#x01B0;a k&#x1EBF;t n&#x1ED1;i',!!x.sta);put('sta-ip',x.sta?x.ip:'--',!!x.sta);put('sta-rssi',x.sta?x.rssi+' dBm':'--',!!x.sta);put('sta-gateway',x.sta?x.gateway:'--',!!x.sta);put('mqtt-state',x.mqtt?'&#x110;&#xE3; k&#x1EBF;t n&#x1ED1;i':'Ch&#x01B0;a k&#x1EBF;t n&#x1ED1;i',!!x.mqtt);document.getElementById('mqtt-client').textContent=x.client_id;document.getElementById('topic-cmd').textContent=x.topics.cmd;document.getElementById('topic-event').textContent=x.topics.event;document.getElementById('topic-status').textContent=x.topics.status}catch(e){put('head-sta','Kh&#xF4;ng &#x0111;&#x1ECD;c &#x0111;&#x01B0;&#x1EE3;c',false)}}async function profiles(){try{const x=await api('/api/wifi-profiles'),box=document.getElementById('profiles');box.textContent='';if(!x.profiles.length){box.textContent='Ch&#x01B0;a c&#xF3; m&#x1EA1;ng &#x0111;&#x1B0;&#x1EE3;c l&#x01B0;u';return}x.profiles.forEach(p=>{const r=document.createElement('div');r.className='profile-row';const s=document.createElement('span');s.textContent=p.ssid;if(p.active){const a=document.createElement('small');a.textContent='&#x0110;ang d&#xF9;ng';s.appendChild(a)}const use=document.createElement('button');use.type='button';use.className='btn';use.textContent='Ch&#x1ECD;n';use.onclick=()=>{document.getElementById('ssid').value=p.ssid;m.className='message ok';m.textContent='&#x110;&#xE3; ch&#x1ECD;n '+p.ssid+'. &#x110;&#x1EC3; tr&#x1ED1;ng m&#x1EAD;t kh&#x1EA9;u n&#x1EBF;u m&#x1EA1;ng &#x0111;&#xE3; &#x111;&#x01B0;&#x1EE3;c nh&#x1EDB;.'};const del=document.createElement('button');del.type='button';del.className='btn delete';del.textContent='X&#xF3;a';del.onclick=async()=>{if(!confirm('X&#xF3;a m&#x1EA1;ng '+p.ssid+'?'))return;try{await api('/api/wifi-profiles/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(p.ssid)});profiles()}catch(e){m.className='message err';m.textContent='Kh&#xF4;ng x&#xF3;a &#x111;&#x1B0;&#x1EE3;c m&#x1EA1;ng.'}};r.append(s,use,del);box.appendChild(r)})}catch(e){document.getElementById('profiles').textContent='Kh&#xF4;ng &#x0111;&#x1ECD;c &#x111;&#x01B0;&#x1EE3;c danh s&#xE1;ch m&#x1EA1;ng'}}document.getElementById('scan').onclick=async()=>{const b=document.getElementById('scan'),t=document.getElementById('scanmsg'),box=document.getElementById('wifi-results');b.disabled=true;t.textContent='&#x110;ang qu&#xE9;t...';box.textContent='';try{const a=await api('/api/wifi-scan');t.textContent=a.length+' m&#x1EA1;ng t&#xEC;m th&#x1EA5;y - ch&#x1EA1;m &#x0111;&#x1EC3; ch&#x1ECD;n';a.forEach(x=>{const r=document.createElement('div');r.className='wifi-row';const pick=document.createElement('button');pick.type='button';pick.textContent=x.ssid;pick.onclick=()=>{document.getElementById('ssid').value=x.ssid;document.getElementById('wifi_pass').focus();m.className='message ok';m.textContent='&#x110;&#xE3; ch&#x1ECD;n '+x.ssid+'. Nh&#x1EAD;p m&#x1EAD;t kh&#x1EA9;u n&#x1EBF;u &#x0111;&#xE2;y l&#xE0; m&#x1EA1;ng m&#x1EDB;i.'};const q=document.createElement('span');q.className='wifi-rssi';q.textContent=x.rssi+' dBm';r.append(pick,q);box.appendChild(r)})}catch(e){t.textContent='Qu&#xE9;t th&#x1EA5;t b&#x1EA1;i'}b.disabled=false};f.onsubmit=async e=>{e.preventDefault();save.disabled=true;m.className='message';m.textContent='&#x110;ang l&#x01B0;u v&#xE0; &#xE1;p d&#x1EE5;ng...';try{const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))});m.className=r.ok?'message ok':'message err';m.textContent=await r.text();if(r.ok){profiles();setTimeout(status,800)}}catch(e){m.className='message err';m.textContent='L&#x01B0;u th&#x1EA5;t b&#x1EA1;i. Ki&#x1EC3;m tra k&#x1EBF;t n&#x1ED1;i.'}save.disabled=false};fetch('/api/session/open',{method:'POST'}).catch(()=>{});setInterval(()=>fetch('/api/session/ping',{method:'POST'}).catch(()=>{}),5000);window.addEventListener('pagehide',()=>{try{navigator.sendBeacon('/api/session/finish','')}catch(e){}});status();profiles();setInterval(status,3000)})();</script></main></body></html>",
        device_name, callbox, callbox, ssid,
        s_config->wifi_dhcp ? "selected" : "", s_config->wifi_dhcp ? "" : "selected",
        s_config->wifi_dhcp ? "hidden" : "", ip, netmask, gateway, dns,
        broker, (unsigned)s_config->mqtt_port, user);

    /* Keep identity and live network state together. The nodes are moved,
     * not duplicated, so their existing IDs and status-update bindings stay
     * exactly the same. */
    static const char identity_style[] =
        "<style>.layout{align-items:start}.hero .device-label,.live{display:none}.layout>.card:first-child>.help{display:none}.identity-name{display:grid;grid-template-columns:minmax(0,1fr) auto minmax(0,1fr);align-items:center;min-height:54px;margin:0 0 12px;padding:9px 12px;border:1px solid var(--line2);border-radius:9px;background:var(--surface2);color:var(--muted);font-size:12px}.identity-name span{justify-self:start}.identity-name b{grid-column:2;color:var(--green);font-size:15px;font-weight:700;text-align:center}.identity-status{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));margin:12px 0 0;border:1px solid var(--line2);border-radius:9px;overflow:hidden}.identity-status .live-item{padding:11px 12px;border-right:1px solid var(--line2);border-bottom:1px solid var(--line2)}.identity-status .live-item:nth-child(2n){border-right:0}.identity-status .live-item:nth-last-child(-n+2){border-bottom:0}.field-label-spacer{display:block;margin-bottom:6px;visibility:hidden;font-size:13px;font-weight:650;line-height:1.45}.wifi-scan-field{display:block}.wifi-scan-field .btn{display:block;width:100%%}.scan-collapse{min-height:36px;margin-top:8px;padding:6px 11px;font-size:12px}.wifi-results.collapsed{display:none}.wifi-manager{margin:12px 0 0;border:1px solid var(--line2);border-radius:9px;background:var(--surface2)}.wifi-manager summary{min-height:46px;display:flex;align-items:center;padding:10px 12px;color:var(--text);font-weight:650;cursor:pointer;list-style:none}.wifi-manager summary::-webkit-details-marker{display:none}.wifi-manager summary:after{content:'+';margin-left:auto;color:var(--green);font-size:20px;font-weight:400}.wifi-manager[open] summary{border-bottom:1px solid var(--line2)}.wifi-manager[open] summary:after{content:'-'} .wifi-manager-content{margin:0;padding:0 10px 10px}.readout{min-height:40px;border:0;border-left:2px solid var(--green);border-radius:0;background:rgba(17,26,44,.48);color:#b7f7dc;cursor:default;pointer-events:none}.mqtt .topic-grid+.topic-grid label:after{content:' (t\u1ef1 sinh)';color:var(--green);font-weight:500}.ap-pill:has(.bad) i{background:var(--red);box-shadow:0 0 0 4px rgba(248,113,113,.12)}@media(max-width:767px){.identity-status{grid-template-columns:1fr}.identity-status .live-item,.identity-status .live-item:nth-child(2n){border-right:0}.identity-status .live-item:last-child{grid-column:auto}}</style>";
    static const char identity_move_script[] =
        "<script>(function(){const card=document.querySelector('.layout>.card');const name=document.querySelector('.hero .device-label');const live=document.querySelector('main>.live');if(card&&name){const value=name.querySelector('b');name.textContent='';const label=document.createElement('span');label.textContent='Tên thiết bị';name.append(label,value);name.className='identity-name';card.insertBefore(name,card.querySelector('.field'))}if(card&&live){live.classList.add('identity-status');card.append(live)}const mqttTitle=document.querySelector('.mqtt h2');if(mqttTitle){const note=document.createElement('span');note.className='mqtt-hint';note.textContent='Theo ID Callbox';mqttTitle.append(note)}document.querySelectorAll('.readout').forEach(e=>{e.setAttribute('aria-readonly','true');e.title='Tu sinh tu ID Callbox, khong the chinh sua'});const profiles=document.getElementById('profiles');const field=profiles&&profiles.closest('.field');if(field){const label=field.querySelector('label');if(label)label.remove();const details=document.createElement('details');details.className='wifi-manager';const summary=document.createElement('summary');summary.textContent='Qu\u1ea3n l\u00fd WiFi \u0111\u00e3 nh\u1edb';field.classList.add('wifi-manager-content');field.parentNode.insertBefore(details,field);details.append(summary,field)}})();</script>";
    static const char alignment_style[] =
        "<style>.identity-name{margin:14px 0 12px}.identity-status .live-item{display:flex;min-height:58px;flex-direction:column;justify-content:center;gap:2px}.identity-status .live-item small{line-height:1.2}.identity-status .live-item b{line-height:1.25}.mqtt h2{margin-bottom:16px}.mqtt-hint{margin-left:auto;padding:5px 9px;border:1px solid rgba(52,211,153,.28);border-radius:999px;background:rgba(52,211,153,.08);color:var(--green);font-size:11px;font-weight:600}.mqtt .topic-grid{align-items:end}.mqtt .topic-grid+.topic-grid{align-items:start}.mqtt .topic-grid+.topic-grid label:after{display:none}.mqtt .topic-grid+.topic-grid>div{min-width:0}.readout{display:flex;align-items:center;min-height:42px;padding:10px 12px;line-height:1.2}@media(max-width:767px){.mqtt-hint{padding:4px 7px;font-size:10px}.identity-status .live-item{min-height:54px}}</style>";
    /* Ethernet is a separate physical uplink.  It occupies a full row in
     * the identity status grid so its connection state and DHCP IP are both
     * visible without making the two-column table uneven. */
    static const char ethernet_status_style[] =
        "<style>.identity-status .ethernet-status{grid-column:1 / -1;border-right:0;background:rgba(52,211,153,.04)}@media(max-width:767px){.identity-status .ethernet-status{grid-column:auto}}</style>";
    static const char ethernet_status_script[] =
        "<script>(function(){const live=document.querySelector('.live');if(!live)return;const item=document.createElement('div');item.className='live-item ethernet-status';item.innerHTML='<small>Ethernet / IP</small><b id=\"eth-state\">\u0110ang ki\u1ec3m tra...</b>';live.append(item);const refresh=async()=>{const e=document.getElementById('eth-state');if(!e)return;try{const x=await fetch('/api/status',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()});e.textContent=x.eth?'\u0110\u00e3 k\u1ebft n\u1ed1i \u00b7 '+x.eth_ip:'Ch\u01b0a k\u1ebft n\u1ed1i';e.className=x.eth?'ok':'bad'}catch(_){e.textContent='Kh\u00f4ng \u0111\u1ecdc \u0111\u01b0\u1ee3c';e.className='bad'}};refresh();setInterval(refresh,10000)})();</script>";
    static const char vertical_layout_style[] =
        "<style>.wifi-top{align-items:end}.wifi-top>.btn{align-self:end;margin-top:0}@media(min-width:1024px){.layout{grid-template-columns:1fr}.layout>.card:nth-child(1){grid-column:1;grid-row:1}.layout>.card:nth-child(2){grid-column:1;grid-row:2}.layout>.card:nth-child(3){grid-column:1;grid-row:3}}@media(max-width:767px){.wifi-top>.btn{align-self:stretch}}</style>";
    static const char identity_id_style[] =
        "<style>.layout>.card:first-child>.field{display:grid;grid-template-columns:minmax(0,1fr) auto minmax(0,1fr);align-items:center;min-height:54px;margin:0;padding:9px 12px;border:1px solid var(--line2);border-radius:9px;background:var(--surface2)}.layout>.card:first-child>.field label{justify-self:start;margin:0}.layout>.card:first-child>.field input{grid-column:2;width:84px;min-height:0;padding:0;border:0;border-radius:0;background:transparent;color:var(--green);font-weight:700;text-align:center}.layout>.card:first-child>.field:focus-within{border-color:var(--cyan);box-shadow:0 0 0 3px rgba(57,205,248,.12)}.layout>.card:first-child>.field input:focus-visible{outline:0;box-shadow:none}@media(max-width:767px){.layout>.card:first-child>.field input{width:68px}}</style>";
    static const char scan_collapse_script[] =
        "<script>(function(){const results=document.getElementById('wifi-results');if(!results)return;const toggle=document.createElement('button');toggle.type='button';toggle.className='btn scan-collapse';toggle.hidden=true;toggle.textContent='Thu g\u1ecdn danh s\u00e1ch';results.insertAdjacentElement('afterend',toggle);const sync=()=>{const has=results.childElementCount>0;toggle.hidden=!has;if(!has)results.classList.remove('collapsed');if(has&&!results.classList.contains('collapsed'))toggle.textContent='Thu g\u1ecdn danh s\u00e1ch'};new MutationObserver(sync).observe(results,{childList:true});toggle.onclick=()=>{const collapsed=results.classList.toggle('collapsed');toggle.textContent=collapsed?'M\u1edf danh s\u00e1ch':'Thu g\u1ecdn danh s\u00e1ch'};sync()})();</script>";
    static const char mqtt_security_style[] =
        "<style>.mqtt-security{display:flex;align-items:end;gap:14px;margin:0 0 2px}.mqtt-security .field{width:min(340px,100%%);margin:0}.mqtt-security span{max-width:540px;padding-bottom:11px;color:var(--muted);font-size:12px}@media(max-width:767px){.mqtt-security{display:block}.mqtt-security .field{width:100%%}.mqtt-security span{display:block;padding:8px 0 0}}</style>";
    static const char sntp_style[] =
        "<style>.sntp-row{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin:12px 0}.sntp-row .field{margin:0}@media(max-width:767px){.sntp-row{grid-template-columns:1fr}}</style>";
    (void)page_insert_before("</head>", identity_style);
    (void)page_insert_before("</head>", alignment_style);
    (void)page_insert_before("</head>", vertical_layout_style);
    (void)page_insert_before("</head>", identity_id_style);
    (void)page_insert_before("</head>", ethernet_status_style);
    (void)page_insert_before("</head>", mqtt_security_style);
    (void)page_insert_before("</head>", sntp_style);
    (void)page_insert_before("</body>", identity_move_script);
    (void)page_insert_before("</body>", ethernet_status_script);
    (void)page_insert_before("</body>", scan_collapse_script);

    /* Add the transport choice without disturbing the stable, compact MQTT
     * field grid. The select remains a normal form element and is saved by
     * the existing FormData code. */
    snprintf(s_mqtt_security_script, sizeof(s_mqtt_security_script),
             "<script>(function(){const grid=document.querySelector('.mqtt .topic-grid');if(!grid)return;const userLabel=document.querySelector('label[for=mqtt_user]'),passLabel=document.querySelector('label[for=mqtt_pass]'),pass=document.getElementById('mqtt_pass'),saved=%s;if(userLabel)userLabel.innerHTML='T&#xE0;i kho&#x1EA3;n MQTT (broker)';if(passLabel)passLabel.innerHTML='M&#x1EAD;t kh&#x1EA9;u MQTT (broker)';if(pass){pass.placeholder='\\u0110\\u1ec3 tr\\u1ed1ng \\u0111\\u1ec3 kh\\u00f4ng d\\u00f9ng m\\u1eadt kh\\u1ea9u';if(saved){pass.value='__CB_KEEP__';pass.addEventListener('focus',()=>{if(pass.value==='__CB_KEEP__')pass.value=''},{once:true})}}const row=document.createElement('div');row.className='mqtt-security';row.innerHTML=\"<div class='field'><label for='mqtt_transport'>B&#x1EA3;o m&#x1EAD;t k&#x1EBF;t n&#x1ED1;i</label><select id='mqtt_transport' name='mqtt_transport'><option value='tcp' %s>TCP - broker n&#x1ED9;i b&#x1ED9;</option><option value='tls' %s>TLS - Cloud / Internet</option></select></div>\";grid.parentNode.insertBefore(row,grid)})();</script>",
             s_config->mqtt_pass[0] ? "true" : "false",
             s_config->mqtt_transport == MQTT_TRANSPORT_TCP ? "selected" : "",
             s_config->mqtt_transport == MQTT_TRANSPORT_TLS ? "selected" : "");
    (void)page_insert_before("</body>", s_mqtt_security_script);

    /* IT can supply an internal NTP IP here; public servers remain the
     * factory fallback. Inputs are normal form fields, persisted in NVS. */
    json_escape(s_config->sntp_primary, s_sntp_primary_json, sizeof(s_sntp_primary_json));
    json_escape(s_config->sntp_fallback, s_sntp_fallback_json, sizeof(s_sntp_fallback_json));
    snprintf(s_sntp_script, sizeof(s_sntp_script),
             "<script>(function(){const card=document.querySelector('.mqtt');if(!card)return;const p=\"%s\",b=\"%s\";const row=document.createElement('div');row.className='sntp-row';row.innerHTML=\"<div class='field'><label for='sntp_primary'>SNTP ch&#x00ED;nh (NTP n&#x1ED9;i b&#x1ED9;)</label><input id='sntp_primary' name='sntp_primary' maxlength='63' required></div><div class='field'><label for='sntp_fallback'>SNTP d&#x1EF1; ph&#x00F2;ng</label><input id='sntp_fallback' name='sntp_fallback' maxlength='63' required></div>\";row.querySelector('#sntp_primary').value=p;row.querySelector('#sntp_fallback').value=b;const anchor=card.querySelector('.mqtt-security')||card.querySelector('.topic-grid');card.insertBefore(row,anchor)})();</script>",
             s_sntp_primary_json, s_sntp_fallback_json);
    (void)page_insert_before("</body>", s_sntp_script);

    /* Status text is filled with textContent at runtime. Decode our compact
     * numeric HTML entities there as well, while keeping SSID values as text
     * nodes (never HTML) for safety. */
    static const char entity_cleanup_script[] =
        "<script>(function(){const d=s=>String(s).replace(/&#x([0-9a-f]+);/gi,(_,x)=>String.fromCodePoint(parseInt(x,16))).replace(/&#([0-9]+);/g,(_,x)=>String.fromCodePoint(+x));const f=n=>{for(const c of n.childNodes){if(c.nodeType===3){const v=d(c.nodeValue);if(v!==c.nodeValue)c.nodeValue=v}else f(c)}};for(const id of ['msg','scanmsg','profiles','wifi-results','head-sta','head-ap','sta-ssid','sta-ip','sta-rssi','sta-gateway','mqtt-state']){const n=document.getElementById(id);if(n)new MutationObserver(()=>f(n)).observe(n,{childList:true,subtree:true,characterData:true})}})();</script>";
    const size_t page_len = strlen(s_page_html);
    const size_t cleanup_len = sizeof(entity_cleanup_script) - 1U;
    char *body_end = strstr(s_page_html, "</body>");
    if (body_end && page_len + cleanup_len + 1U < sizeof(s_page_html)) {
        const size_t offset = (size_t)(body_end - s_page_html);
        memmove(body_end + cleanup_len, body_end, page_len - offset + 1U);
        memcpy(body_end, entity_cleanup_script, cleanup_len);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_page_html, HTTPD_RESP_USE_STRLEN);
}

/* GET / — mọi client chưa đăng nhập nhận trang login; client có cookie hợp lệ
 * được mở phiên cấu hình và nhận giao diện hiện đại. */
static esp_err_t root_handler(httpd_req_t *req)
{
    if (!request_is_authorized(req)) return send_login_page(req, NULL);
    if (request_from_local_ap(req)) {
        s_session_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(PORTAL_SESSION_TIMEOUT_MS);
    }
    return portal_page_handler_modern(req);
}

/* POST /login — xử lý đăng nhập portal: đọc thân form (username,
 * password), kiểm tra với admin/web_password, sinh token ngẫu nhiên và
 * gắn cookie 'cb_auth' hết hạn 30 phút, sau đó redirect về /. */
static esp_err_t login_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len >= 256) {
        return send_login_page(req, "<p class='error'>Du lieu dang nhap khong hop le.</p>");
    }

    char body[256];
    if (portal_receive_body(req, body, sizeof(body)) != ESP_OK) {
        return send_login_page(req, "<p class='error'>Het thoi gian doc du lieu dang nhap.</p>");
    }

    /* Trích username + password từ form; tách riêng 2 điều kiện để log rõ
     * phần nào sai (giúp kỹ thuật viên debug). */
    char username[32] = { 0 };
    char password[64] = { 0 };
    const bool form_ok = form_value(body, "username", username, sizeof(username)) &&
                         form_value(body, "password", password, sizeof(password));
    const bool user_ok = form_ok && strcmp(username, PORTAL_STA_USERNAME) == 0;
    const bool password_ok = form_ok && s_config &&
                             strcmp(password, s_config->web_password) == 0;
    if (!user_ok || !password_ok) {
        ESP_LOGW(TAG, "Portal login rejected (user=%d password=%d)", user_ok, password_ok);
        return send_login_page(req, "<p class='error'>Sai tai khoan hoac mat khau.</p>");
    }

    /* Đăng nhập thành công: sinh token 32 hex ngẫu nhiên (esp_random ×4)
     * và đặt hạn cookie 30 phút (PORTAL_AUTH_TIMEOUT_MS). */
    snprintf(s_auth_token, sizeof(s_auth_token), "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
    s_auth_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(PORTAL_AUTH_TIMEOUT_MS);

    /* Gửi Set-Cookie: cb_auth=<token>; chỉ HTTP (javascript không đọc),
     * SameSite=Strict chống CSRF; Max-Age 1800s (=30 phút). */
    char cookie[128];
    snprintf(cookie, sizeof(cookie), PORTAL_AUTH_COOKIE "=%s; Path=/; Max-Age=1800; HttpOnly; SameSite=Strict",
             s_auth_token);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    ESP_LOGI(TAG, "Portal login accepted; session expires in 30 minutes");
    return httpd_resp_send(req, NULL, 0);
}

/* POST /save — nhận form cấu hình từ trang portal, chuẩn hóa & lưu.
 * Áp dụng trực tiếp WiFi mới (không reboot); đồng thời lưu xuống NVS.
 * Lưu ý: đoạn mã sau return (ESP_OK) là mã rác cũ (không bao giờ chạy). */
static esp_err_t save_handler(httpd_req_t *req)
{
    if (!require_portal_access(req)) return ESP_OK;
    /* Giới hạn kích thước form: tổng cấu hình hợp lệ cho ~1 KB */
    if (req->content_len == 0 || req->content_len >= 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form size");
        return ESP_OK;
    }

    /* Đọc toàn bộ thân form (Content-Length byte) vào body */
    char body[1024];
    if (portal_receive_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request read timed out");
        return ESP_OK;
    }

    /* Làm việc trên bản sao cấu hình; chỉ gắn vào *s_config khi lưu thành công */
    const Config_t previous = *s_config;
    Config_t updated = previous;
    char value[128];
    if (form_value(body, "callbox_id", value, sizeof(value)) && value[0]) {
        /* Chỉ cho ID gồm chữ số (đặt tên thiết bị AUBOT-Callbox-<số>) */
        if (!valid_numeric_callbox_id(value)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Callbox ID must contain digits only");
            return ESP_OK;
        }
        strncpy(updated.callbox_id, value, sizeof(updated.callbox_id) - 1);
        updated.callbox_id[sizeof(updated.callbox_id) - 1] = '\0';
    }
    /* Xử lý WiFi: nếu nhập mật khẩu mới → dùng luôn; nếu để trống →
     * tái sử dụng mật khẩu của mạng đã nhớ (hoặc mạng đang dùng) */
    char wifi_ssid[33] = { 0 };
    char wifi_password[64] = { 0 };
    bool wifi_ssid_present = form_value(body, "wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
    bool wifi_password_present = form_value(body, "wifi_pass", wifi_password, sizeof(wifi_password));
    if (wifi_ssid_present && wifi_ssid[0]) {
        char remembered_password[64] = { 0 };
        if (!wifi_password_present || !wifi_password[0]) {
            /* Tìm mật khẩu đã nhớ cho SSID này trong danh sách profile */
            if (!config_find_wifi_password(&updated, wifi_ssid, remembered_password,
                                           sizeof(remembered_password))) {
                /* Chưa nhớ: chỉ được để trống nếu đây là mạng đang dùng */
                if (strcmp(wifi_ssid, updated.wifi_ssid) == 0) {
                    strncpy(remembered_password, updated.wifi_pass, sizeof(remembered_password) - 1);
                } else {
                    /* Mạng mới + trống mật khẩu → báo lỗi yêu cầu nhập */
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                        "Enter the password for a new WiFi network");
                    return ESP_OK;
                }
            }
            strncpy(wifi_password, remembered_password, sizeof(wifi_password) - 1);
        }
        wifi_password[sizeof(wifi_password) - 1] = '\0';
        /* Thêm/chèn mạng vào danh sách profile (mạng mới lên đầu, ưu tiên) */
        config_add_wifi_profile(&updated, wifi_ssid, wifi_password);
    }
    if (form_value(body, "wifi_dhcp", value, sizeof(value))) {
        updated.wifi_dhcp = strcmp(value, "0") != 0;
    }
    if (form_value(body, "wifi_ip", value, sizeof(value))) {
        strncpy(updated.wifi_ip, value, sizeof(updated.wifi_ip) - 1);
        updated.wifi_ip[sizeof(updated.wifi_ip) - 1] = '\0';
    }
    if (form_value(body, "wifi_netmask", value, sizeof(value))) {
        strncpy(updated.wifi_netmask, value, sizeof(updated.wifi_netmask) - 1);
        updated.wifi_netmask[sizeof(updated.wifi_netmask) - 1] = '\0';
    }
    if (form_value(body, "wifi_gateway", value, sizeof(value))) {
        strncpy(updated.wifi_gateway, value, sizeof(updated.wifi_gateway) - 1);
        updated.wifi_gateway[sizeof(updated.wifi_gateway) - 1] = '\0';
    }
    if (form_value(body, "wifi_dns", value, sizeof(value))) {
        strncpy(updated.wifi_dns, value, sizeof(updated.wifi_dns) - 1);
        updated.wifi_dns[sizeof(updated.wifi_dns) - 1] = '\0';
    }
    if (form_value(body, "sntp_primary", value, sizeof(value))) {
        if (!valid_sntp_host(value)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid primary SNTP server");
            return ESP_OK;
        }
        strncpy(updated.sntp_primary, value, sizeof(updated.sntp_primary) - 1);
        updated.sntp_primary[sizeof(updated.sntp_primary) - 1] = '\0';
    }
    if (form_value(body, "sntp_fallback", value, sizeof(value))) {
        if (!valid_sntp_host(value)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid fallback SNTP server");
            return ESP_OK;
        }
        strncpy(updated.sntp_fallback, value, sizeof(updated.sntp_fallback) - 1);
        updated.sntp_fallback[sizeof(updated.sntp_fallback) - 1] = '\0';
    }
    if (!updated.wifi_dhcp &&
        (!valid_ipv4(updated.wifi_ip) || !valid_ipv4(updated.wifi_netmask) ||
         !valid_ipv4(updated.wifi_gateway) || !valid_ipv4(updated.wifi_dns))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Static mode requires valid IP, netmask, gateway and DNS");
        return ESP_OK;
    }
    /* MQTT: broker, cổng (1..65535), user; mật khẩu chỉ cập nhật khi nhập mới */
    if (form_value(body, "mqtt_broker", value, sizeof(value)) && value[0]) {
        strncpy(updated.mqtt_broker, value, sizeof(updated.mqtt_broker) - 1);
        updated.mqtt_broker[sizeof(updated.mqtt_broker) - 1] = '\0';
    }
    if (form_value(body, "mqtt_port", value, sizeof(value))) {
        long port = strtol(value, NULL, 10);
        if (port < 1 || port > 65535) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "MQTT port must be 1..65535");
            return ESP_OK;
        }
        updated.mqtt_port = (uint16_t)port;
    }
    if (form_value(body, "mqtt_transport", value, sizeof(value))) {
        if (strcmp(value, "tcp") == 0) {
            updated.mqtt_transport = MQTT_TRANSPORT_TCP;
        } else if (strcmp(value, "tls") == 0) {
            updated.mqtt_transport = MQTT_TRANSPORT_TLS;
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MQTT transport");
            return ESP_OK;
        }
    }
    if (form_value(body, "mqtt_user", value, sizeof(value))) {
        strncpy(updated.mqtt_user, value, sizeof(updated.mqtt_user) - 1);
        updated.mqtt_user[sizeof(updated.mqtt_user) - 1] = '\0';
    }
    if (form_value(body, "mqtt_pass", value, sizeof(value)) &&
        strcmp(value, PORTAL_MQTT_PASS_RETAIN_MARKER) != 0) {
        strncpy(updated.mqtt_pass, value, sizeof(updated.mqtt_pass) - 1);
        updated.mqtt_pass[sizeof(updated.mqtt_pass) - 1] = '\0';
    }

    /* Lưu toàn bộ cấu hình xuống NVS trước khi áp dụng */
    esp_err_t err = callbox_config_store_save(&updated);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save configuration");
        return ESP_OK;
    }

    /* Gắn bản đã cập nhật. Chỉ áp lại STA khi cấu hình Wi-Fi thực sự đổi;
     * ID/MQTT chỉ cần lưu, không được làm ngắt liên kết nhà máy đang chạy. */
    *s_config = updated;
    const bool wifi_changed = wifi_runtime_config_changed(&previous, &updated);
    const bool mqtt_changed = mqtt_runtime_config_changed(&previous, &updated);
    if (wifi_changed) {
        esp_err_t apply_err = wifi_apply_config(&updated);
        if (apply_err != ESP_OK) {
            ESP_LOGW(TAG, "Configuration saved but Wi-Fi apply is pending: %s",
                     esp_err_to_name(apply_err));
        }
        ESP_LOGI(TAG, "Wi-Fi settings changed; applied STA configuration");
    } else {
        ESP_LOGI(TAG, "Saved ID/MQTT settings; existing STA connection kept intact");
    }
    if (mqtt_changed) {
        mqtt_client_reconfigure(&updated);
        ESP_LOGI(TAG, "MQTT endpoint, security, credentials or logical ID changed; reconnecting MQTT");
    }
    if (strcmp(previous.sntp_primary, updated.sntp_primary) != 0 ||
        strcmp(previous.sntp_fallback, updated.sntp_fallback) != 0) {
        time_sync_reconfigure(&updated);
        ESP_LOGI(TAG, "SNTP servers updated without interrupting Wi-Fi or MQTT");
    }
    play_config_saved_tone();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_sendstr(req,
                       "Da luu cau hinh. WiFi dang duoc ap dung; AP van san sang."
                       " Hay bam Ket thuc cai dat khi hoan tat.");
    return ESP_OK;
    httpd_resp_sendstr(req, "<html><body><h2>Ã„ÂÃƒÂ£ lÃ†Â°u cÃ¡ÂºÂ¥u hÃƒÂ¬nh</h2><p>Callbox Ã„â€˜ang khÃ¡Â»Å¸i Ã„â€˜Ã¡Â»â„¢ng lÃ¡ÂºÂ¡i...</p></body></html>");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

/* Khởi động HTTP server portal (nếu chưa chạy) và đăng ký toàn bộ route.
 * Gọi một lần khi AP cấu hình sẵn sàng (callback từ wifi_init). */
esp_err_t config_portal_start(Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = config;

    /* Idempotent: nếu server đã chạy thì không khởi động lại */
    if (s_server) return ESP_OK;

    /* Cấu hình server: tăng max_uri_handlers cho đủ 13 route + stack an toàn */
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 15;
    server_config.stack_size = 8192;
    server_config.recv_wait_timeout = 2;
    server_config.send_wait_timeout = 2;

    esp_err_t err = httpd_start(&s_server, &server_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    /* Đăng ký route cơ bản: trang chính, đăng nhập, logo, lưu cấu hình */
    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t login_uri = {
        .uri = "/login",
        .method = HTTP_POST,
        .handler = login_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t logo_uri = {
        .uri = "/logo.jpg",
        .method = HTTP_GET,
        .handler = logo_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t scan_uri = {
        .uri = "/api/wifi-scan",
        .method = HTTP_GET,
        .handler = wifi_scan_handler,
        .user_ctx = NULL,
    };
    /* Đăng ký route API: quét WiFi, cấu hình JSON, trạng thái I/O, trạng thái hệ thống */
    const httpd_uri_t config_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_json_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t io_status_uri = {
        .uri = "/api/io-status",
        .method = HTTP_GET,
        .handler = io_status_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t system_status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = system_status_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t wifi_profiles_uri = {
        .uri = "/api/wifi-profiles",
        .method = HTTP_GET,
        .handler = wifi_profiles_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t wifi_profile_delete_uri = {
        .uri = "/api/wifi-profiles/delete",
        .method = HTTP_POST,
        .handler = wifi_profile_delete_handler,
        .user_ctx = NULL,
    };
    /* Đăng ký route phiên: mở / ping (gia hạn) / kết thúc phiên AP */
    const httpd_uri_t session_open_uri = {
        .uri = "/api/session/open", .method = HTTP_POST,
        .handler = session_open_handler, .user_ctx = NULL,
    };
    const httpd_uri_t session_ping_uri = {
        .uri = "/api/session/ping", .method = HTTP_POST,
        .handler = session_ping_handler, .user_ctx = NULL,
    };
    const httpd_uri_t session_finish_uri = {
        .uri = "/api/session/finish", .method = HTTP_POST,
        .handler = session_finish_handler, .user_ctx = NULL,
    };
    /* Đăng ký toàn bộ handler vào server — chỉ đăng ký, không cần giữ handle */
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &login_uri);
    httpd_register_uri_handler(s_server, &logo_uri);
    httpd_register_uri_handler(s_server, &save_uri);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &config_uri);
    httpd_register_uri_handler(s_server, &io_status_uri);
    httpd_register_uri_handler(s_server, &system_status_uri);
    httpd_register_uri_handler(s_server, &wifi_profiles_uri);
    httpd_register_uri_handler(s_server, &wifi_profile_delete_uri);
    httpd_register_uri_handler(s_server, &session_open_uri);
    httpd_register_uri_handler(s_server, &session_ping_uri);
    httpd_register_uri_handler(s_server, &session_finish_uri);

    ESP_LOGI(TAG, "Configuration portal ready at http://%s/", CALLBOX_AP_IP_ADDR);
    return ESP_OK;
}
