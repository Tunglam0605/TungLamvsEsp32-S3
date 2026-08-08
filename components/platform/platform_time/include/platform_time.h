/**
 * @file    platform_time.h
 * @brief   Dịch vụ đồng hồ nền tảng: cấu hình SNTP + timezone chung.
 *
 *          Component platform_time sở hữu toàn bộ vòng đời SNTP của ESP-IDF
 *          (lưu tên máy chủ, áp dụng timezone, init/stop). Caller chỉ cung
 *          cấp cấu hình qua platform_time_config_t — không cần biết bất kỳ
 *          chi tiết ESP-IDF SNTP nào.
 *
 *          ═══ VÒNG ĐỜI ═══
 *          - platform_time_init(): khởi tạo lần đầu (validate → copy config
 *            → áp dụng timezone → cấu hình + start SNTP). Idempotent: gọi
 *            lại sau khi đã start thì trả về ESP_OK mà không chạm SNTP.
 *          - platform_time_reconfigure(): thay thế máy chủ SNTP đang chạy
 *            (stop → copy config mới → start). Nếu chưa từng init thì behave
 *            như init.
 *          - platform_time_is_valid(): kiểm tra RTC đã vượt mốc epoch tối
 *            thiểu do caller truyền hay chưa.
 *
 *          ═══ QUYỀN SỞ HỮU STRING ═══
 *          Platform COPY mọi chuỗi trong config vào buffer tĩnh nội bộ của
 *          mình. ESP-IDF SNTP (lwIP) giữ con trỏ trỏ tới các chuỗi máy chủ,
 *          vì vậy các con trỏ từ caller (Config_t, HTTP handler stack...) chỉ
 *          được dùng tạm trong lời gọi và được thay bằng bản copy lâu dài
 *          trước khi SNTP bắt đầu dùng.
 *
 *          ═══ ĐỘC LẬP ═══
 *          Component này KHÔNG biết: Config_t, g_config, queues.h, CallBox,
 *          MQTT, Wi-Fi, Ethernet, BSP, portal, business state. Mọi policy
 *          (máy chủ mặc định, timezone, mốc epoch hợp lệ) là của caller.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#ifndef PLATFORM_TIME_H
#define PLATFORM_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>      /* time_t */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Cấu hình thời gian cho platform (không phụ thuộc sản phẩm). */
typedef struct {
    const char *primary_server;   /**< Máy chủ SNTP chính (bắt buộc, non-empty) */
    const char *fallback_server;  /**< Máy chủ SNTP phụ (tùy chọn, có thể NULL) */
    const char *timezone;         /**< Timezone POSIX TZ (vd. "ICT-7") */
} platform_time_config_t;

/**
 * @brief  Khởi tạo dịch vụ đồng hồ: validate + copy config, áp dụng timezone,
 *         cấu hình và start SNTP.
 *
 * @param  config  Cấu hình SNTP/timezone. primary_server phải non-NULL và
 *                 non-empty; timezone phải non-NULL (có thể empty => bỏ qua
 *                 setenv). fallback_server có thể NULL/empty (bỏ qua máy phụ).
 * @return ESP_OK                  thành công
 * @return ESP_ERR_INVALID_ARG     config NULL / primary / timezone không hợp lệ
 * @return ESP_ERR_NO_MEM          không cấp được bộ nhớ SNTP
 * @return ESP_ERR_TIMEOUT         SNTP start hết thời gian chờ (v6.1-dev)
 */
esp_err_t platform_time_init(const platform_time_config_t *config);

/**
 * @brief  Thay máy chủ SNTP đang chạy mà không reboot (stop → copy mới → start).
 *         Nếu dịch vụ chưa start thì hoạt động như init.
 *
 * @param  config  Cấu hình mới (cùng ràng buộc như platform_time_init).
 * @return ESP_OK / ESP_ERR_INVALID_ARG / ESP_ERR_NO_MEM / ESP_ERR_TIMEOUT
 */
esp_err_t platform_time_reconfigure(const platform_time_config_t *config);

/**
 * @brief  Kiểm tra RTC đã chứa timestamp hợp lệ hay chưa.
 *
 * @param  minimum_epoch  Mốc epoch Unix (giây) tối thiểu coi là hợp lệ.
 *                        Mốc này là quyết định của caller (policy ứng dụng).
 * @return true nếu time(NULL) >= minimum_epoch.
 */
bool platform_time_is_valid(time_t minimum_epoch);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_TIME_H */
