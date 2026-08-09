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
 *          ═══ HỢP ĐỒNG CHUỖI ═══
 *          Mọi chuỗi phải FIT HOÀN TOÀN trong buffer nội bộ (tối đa
 *          PLATFORM_TIME_SERVER_BUF_LEN - 1 ký tự). Chuỗi quá dài bị
 *          REJECT với ESP_ERR_INVALID_ARG — không bao giờ cắt ngầm rồi
 *          khởi động SNTP với giá trị méo.
 *
 *          ═══ HỢP ĐỒNG TIMEZONE ═══
 *          timezone bắt buộc non-NULL và non-empty. Caller muốn UTC phải
 *          truyền explicit POSIX TZ (vd. "UTC0"). Chuỗi rỗng bị reject.
 *
 *          ═══ ĐỘC LẬP ═══
 *          Component này không include/bíết gì về: Config_t, g_config,
 *          queues.h, CallBox, MQTT, Wi-Fi, Ethernet, BSP, portal, business.
 *          SNTP tự retry khi mạng chưa lên — platform không cần biết transport.
 *
 *          ═══ LIFECYCLE (TRUTHFUL) ═══
 *          platform_time_start() là primitive "attempt start" — KHÔNG quản lý
 *          global state. Ownership state nằm ở public lifecycle functions:
 *            - init: s_started = true CHỈ SAU KHI start thành công
 *            - reconfigure: stop xong → s_started = false ngay; start mới
 *              thành công → true
 *          Không bao giờ fake running state.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.2
 * @date    2026
 */
#include "platform_time.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include <stdlib.h>   /* setenv */
#include <string.h>   /* strlen, strncpy */
#include <time.h>     /* tzset */

static const char *TAG = "PLATFORM_TIME";

/* Buffer tĩnh lâu dài: SNTP giữ con trỏ tới đây, không bao giờ trỏ ra ngoài.
 * Chuỗi hợp lệ phải dài tối đa BUF_LEN - 1 ký tự (còn null terminator). */
#define PLATFORM_TIME_SERVER_BUF_LEN 64U

static char s_primary[PLATFORM_TIME_SERVER_BUF_LEN];
static char s_fallback[PLATFORM_TIME_SERVER_BUF_LEN];
static char s_timezone[PLATFORM_TIME_SERVER_BUF_LEN];
static bool s_started;

/* Sao chép chuỗi an toàn vào buffer tĩnh của platform (luôn null-terminate).
 * Caller phải validate length trước — hàm này không cắt ngầm. */
static void platform_time_copy(char *dst, size_t dst_len, const char *src)
{
    strncpy(dst, src, dst_len - 1U);
    dst[dst_len - 1U] = '\0';
}

/* Validate toàn bộ cấu hình TRƯỚC khi chạm service:
 *   - primary: bắt buộc non-NULL, non-empty, fit buffer
 *   - fallback: optional (NULL/empty bỏ qua), nếu có phải fit buffer
 *   - timezone: bắt buộc non-NULL, non-empty, fit buffer
 * Không cắt ngầm chuỗi quá dài. */
static esp_err_t platform_time_config_validate(const platform_time_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->primary_server || config->primary_server[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(config->primary_server) >= PLATFORM_TIME_SERVER_BUF_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->fallback_server &&
        strlen(config->fallback_server) >= PLATFORM_TIME_SERVER_BUF_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->timezone || config->timezone[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(config->timezone) >= PLATFORM_TIME_SERVER_BUF_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void platform_time_apply_timezone(const char *tz)
{
    /* Timezone là giá trị caller cung cấp (vd. "ICT-7" hoặc "UTC0"); platform
     * chỉ áp dụng cơ chế hệ thống. Validation đã đảm bảo chuỗi non-empty. */
    setenv("TZ", tz, 1);
    tzset();
}

static esp_err_t platform_time_start(const platform_time_config_t *config)
{
    esp_err_t ret = platform_time_config_validate(config);
    if (ret != ESP_OK) {
        return ret;
    }

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

esp_err_t platform_time_init(const platform_time_config_t *config)
{
    /* Idempotent: đã start rồi thì gọi lại là no-op, không start SNTP 2 lần. */
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t ret = platform_time_start(config);
    if (ret == ESP_OK) {
        s_started = true;   /* Commit state CHỈ khi start thật sự thành công. */
    }
    return ret;
}

esp_err_t platform_time_reconfigure(const platform_time_config_t *config)
{
    /* Validate TRƯỚC khi chạm service: config mới xấu thì không phá service
     * đang chạy (không stop rồi mới trả lỗi). */
    esp_err_t ret = platform_time_config_validate(config);
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_started) {
        /* Lifecycle truthful: ngay sau khi SNTP dừng, state về false —
         * không giữ "đang chạy" ảo trong khoảng stop→start. */
        esp_sntp_stop();
        s_started = false;
    }

    ret = platform_time_start(config);
    if (ret == ESP_OK) {
        s_started = true;   /* Chỉ đánh dấu khi start thật sự thành công. */
    }
    return ret;
}

bool platform_time_is_valid(time_t minimum_epoch)
{
    return time(NULL) >= minimum_epoch;
}
