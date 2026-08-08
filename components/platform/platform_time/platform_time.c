/**
 * @file    platform_time.c
 * @brief   Triển khai dịch vụ đồng hồ nền tảng (SNTP + timezone).
 *
 *          ═══ SỞ HỮU STRING — QUAN TRỌNG ═══
 *          lwIP SNTP giữ RAW POINTER tới tên máy chủ (sntp_servers[idx].name).
 *          Do đó mọi chuỗi từ caller chỉ được dùng tạm để copy vào buffer
 *          tĩnh của platform (s_primary / s_fallback / s_timezone) TRƯỚC khi
 *          esp_sntp_setservername() / setenv() được gọi. Sau khi hàm trả về,
 *          SNTP vẫn trỏ vào buffer platform — không bao giờ trỏ vào bộ nhớ
 *          tạm của caller.
 *
 *          ═══ ĐỘC LẬP ═══
 *          Component này không include/bíết gì về: Config_t, g_config,
 *          queues.h, CallBox, MQTT, Wi-Fi, Ethernet, BSP, portal, business.
 *          SNTP tự retry khi mạng chưa lên — platform không cần biết transport.
 *
 *          ═══ LIFECYCLE ═══
 *          s_started = true CHỈ SAU KHI mọi bước (validate → copy → timezone
 *          → SNTP start) đã thành công. Nếu lỗi giữa chừng, state phản ánh
 *          đúng sự thật: không fake success.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#include "platform_time.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include <stdlib.h>   /* setenv */
#include <string.h>   /* strncpy */
#include <time.h>     /* tzset */

static const char *TAG = "PLATFORM_TIME";

/* Buffer tĩnh lâu dài: SNTP giữ con trỏ tới đây, không bao giờ trỏ ra ngoài. */
#define PLATFORM_TIME_SERVER_BUF_LEN 64U

static char s_primary[PLATFORM_TIME_SERVER_BUF_LEN];
static char s_fallback[PLATFORM_TIME_SERVER_BUF_LEN];
static char s_timezone[PLATFORM_TIME_SERVER_BUF_LEN];
static bool s_started;

/* Sao chép chuỗi an toàn vào buffer tĩnh của platform (luôn null-terminate). */
static void platform_time_copy(char *dst, size_t dst_len, const char *src)
{
    strncpy(dst, src, dst_len - 1U);
    dst[dst_len - 1U] = '\0';
}

static bool platform_time_config_valid(const platform_time_config_t *config)
{
    if (!config) {
        return false;
    }
    if (!config->primary_server || config->primary_server[0] == '\0') {
        return false;
    }
    if (!config->timezone) {
        return false;
    }
    return true;
}

static void platform_time_apply_timezone(const char *tz)
{
    /* Timezone là giá trị caller cung cấp (vd. "ICT-7"); platform chỉ áp dụng
     * cơ chế hệ thống. TZ rỗng => giữ UTC, không đặt lại môi trường. */
    if (tz[0] != '\0') {
        setenv("TZ", tz, 1);
        tzset();
    }
}

static esp_err_t platform_time_start(const platform_time_config_t *config)
{
    /* BƯỚC 1 — Copy mọi chuỗi vào buffer platform (caller config có thể là
     * bộ nhớ tạm: HTTP handler stack, Config_t local...). */
    platform_time_copy(s_primary, sizeof(s_primary), config->primary_server);
    if (config->fallback_server && config->fallback_server[0] != '\0') {
        platform_time_copy(s_fallback, sizeof(s_fallback), config->fallback_server);
    } else {
        s_fallback[0] = '\0';
    }
    platform_time_copy(s_timezone, sizeof(s_timezone), config->timezone);

    /* BƯỚC 2 — Áp dụng timezone trước khi khởi động SNTP. */
    platform_time_apply_timezone(s_timezone);

    /* BƯỚC 3 — Cấu hình SNTP: chế độ poll, server chính + phụ.
     * esp_sntp_setservername() giữ con trỏ => truyền buffer tĩnh của platform. */
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_primary);
    if (s_fallback[0] != '\0') {
        esp_sntp_setservername(1, s_fallback);
    } else {
        esp_sntp_setservername(1, NULL);
    }
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started: primary=%s fallback=%s tz=%s",
             s_primary, s_fallback, s_timezone);
    return ESP_OK;
}

static esp_err_t platform_time_init_unlocked(const platform_time_config_t *config)
{
    if (!platform_time_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    return platform_time_start(config);
}

esp_err_t platform_time_init(const platform_time_config_t *config)
{
    /* Idempotent: đã start rồi thì gọi lại là no-op, không start SNTP 2 lần. */
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t ret = platform_time_init_unlocked(config);
    if (ret == ESP_OK) {
        s_started = true;   /* Đánh dấu CHỈ SAU khi mọi bước thành công. */
    }
    return ret;
}

esp_err_t platform_time_reconfigure(const platform_time_config_t *config)
{
    if (!platform_time_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_started) {
        /* Đang chạy: stop trước, sau đó copy config mới và start lại.
         * Nếu start mới thất bại, s_started vẫn giữ true (SNTP stack đã được
         * esp_sntp_init trước đó; lỗi ở đây thường do hết bộ nhớ mạng tạm thời,
         * SNTP cũ đã dừng và caller có thể thử lại). */
        esp_sntp_stop();
        esp_err_t ret = platform_time_start(config);
        if (ret == ESP_OK) {
            s_started = true;
        }
        return ret;
    }

    /* Chưa từng start: behave như init. */
    esp_err_t ret = platform_time_start(config);
    if (ret == ESP_OK) {
        s_started = true;
    }
    return ret;
}

bool platform_time_is_valid(time_t minimum_epoch)
{
    return time(NULL) >= minimum_epoch;
}
