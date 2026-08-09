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
 *            như init. Lifecycle truthful: ngay sau khi SNTP stop, state
 *            chuyển về "chưa chạy"; chỉ trở lại "đang chạy" khi start mới
 *            thành công.
 *          - platform_time_is_valid(): kiểm tra RTC đã vượt mốc epoch tối
 *            thiểu do caller truyền hay chưa.
 *
 *          ═══ ERROR CONTRACT (truthful) ═══
 *          Các API ESP-IDF SNTP được dùng (esp_sntp_setoperatingmode /
 *          esp_sntp_setservername / esp_sntp_init / esp_sntp_stop) trả void,
 *          nên platform_time không thể propagate lỗi nội bộ của SNTP.
 *          Do đó contract chỉ hứa:
 *            - ESP_OK
 *            - ESP_ERR_INVALID_ARG (config / chuỗi không hợp lệ)
 *          Không quảng cáo ESP_ERR_NO_MEM / ESP_ERR_TIMEOUT — implementation
 *          không có đường thực tế nào trả chúng. Nếu tương lai ESP-IDF API
 *          cho phép propagate error, contract mới được mở rộng.
 *
 *          ═══ QUYỀN SỞ HỮU STRING ═══
 *          Platform COPY mọi chuỗi trong config vào buffer tĩnh nội bộ của
 *          mình. ESP-IDF SNTP (lwIP) giữ con trỏ trỏ tới các chuỗi máy chủ,
 *          vì vậy các con trỏ từ caller (Config_t, HTTP handler stack...) chỉ
 *          được dùng tạm trong lời gọi và được thay bằng bản copy lâu dài
 *          trước khi SNTP bắt đầu dùng.
 *
 *          Mỗi chuỗi phải FIT HOÀN TOÀN trong buffer nội bộ (tối đa 63 ký
 *          tự + null). Chuỗi quá dài bị REJECT (ESP_ERR_INVALID_ARG) — không
 *          bao giờ bị cắt ngầm rồi mới khởi động SNTP.
 *
 *          ═══ TIMEZONE ═══
 *          timezone bắt buộc non-NULL và non-empty. Caller muốn UTC phải
 *          truyền explicit POSIX TZ (vd. "UTC0") — chuỗi rỗng "" không được
 *          chấp nhận (ambiguous).
 *
 *          ═══ ĐỘC LẬP ═══
 *          Component này KHÔNG biết: Config_t, g_config, queues.h, CallBox,
 *          MQTT, Wi-Fi, Ethernet, BSP, portal, business state. Mọi policy
 *          (máy chủ mặc định, timezone, mốc epoch hợp lệ) là của caller.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.2
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
    const char *timezone;         /**< Timezone POSIX TZ bắt buộc (vd. "ICT-7", "UTC0") */
} platform_time_config_t;

/**
 * @brief  Khởi tạo dịch vụ đồng hồ: validate + copy config, áp dụng timezone,
 *         cấu hình và start SNTP.
 *
 * @param  config  Cấu hình SNTP/timezone. Ràng buộc:
 *                 - primary_server: bắt buộc non-NULL, non-empty, ≤ 63 ký tự
 *                 - fallback_server: optional (NULL/empty bỏ qua máy phụ),
 *                   nếu có phải ≤ 63 ký tự
 *                 - timezone: bắt buộc non-NULL, non-empty, ≤ 63 ký tự
 *                   (muốn UTC truyền "UTC0", không dùng "")
 *                 Chuỗi vượt buffer bị REJECT — không bao giờ cắt ngầm.
 * @return ESP_OK               thành công
 * @return ESP_ERR_INVALID_ARG  config NULL / primary / timezone không hợp lệ /
 *                              chuỗi quá dài
 */
esp_err_t platform_time_init(const platform_time_config_t *config);

/**
 * @brief  Thay máy chủ SNTP đang chạy mà không reboot (stop → copy mới → start).
 *         Nếu dịch vụ chưa start thì hoạt động như init.
 *
 *         Lifecycle truthful: SNTP stop xong là s_started chuyển false ngay;
 *         chỉ quay lại true khi start mới thành công. Config mới được validate
 *         TRƯỚC khi stop — config xấu không phá service đang chạy.
 *
 * @param  config  Cấu hình mới (cùng ràng buộc như platform_time_init).
 * @return ESP_OK               thành công
 * @return ESP_ERR_INVALID_ARG  config không hợp lệ
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
