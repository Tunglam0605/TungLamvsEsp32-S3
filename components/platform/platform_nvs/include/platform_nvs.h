/**
 * @file    platform_nvs.h
 * @brief   Dịch vụ NVS nền tảng: lifecycle + typed access cho ESP-IDF NVS.
 *
 *          Component platform_nvs sở hữu toàn bộ provider mechanics của
 *          ESP-IDF NVS (nvs_flash_init / nvs_open / nvs_get_* / nvs_set_* /
 *          nvs_commit / nvs_close / nvs_flash_erase). Caller (product
 *          persistence adapter) chỉ cung cấp namespace + key + dữ liệu —
 *          không cần biết bất kỳ chi tiết ESP-IDF NVS nào.
 *
 *          ═══ VÒNG ĐỜI ═══
 *          - platform_nvs_init(): khởi tạo NVS flash một lần. Nếu partition
 *            bị hỏng (NO_FREE_PAGES / NEW_VERSION_FOUND) thì erase rồi init
 *            lại — recovery như ESP-IDF khuyến nghị. Không bao giờ tự erase
 *            ngoài hai trường hợp đó.
 *          - platform_nvs_erase_all(): xóa TOÀN BỘ partition NVS.
 *
 *          ═══ TRANSACTION / HANDLE ═══
 *          Một "namespace session" giữ nguyên handle qua nhiều thao tác:
 *              platform_nvs_open(...)  → 1 lần
 *              platform_nvs_set_*()    → nhiều lần
 *              platform_nvs_commit()   → 1 lần (chỉ sau khi mọi SET thành công)
 *              platform_nvs_close()    → 1 lần
 *          Cách này giữ nguyên atomicity thực tế và hạn chế flash wear —
 *          KHÔNG commit sau từng key. Handle là struct nhỏ, deterministic
 *          (không cấp phát heap) và được caller khai báo trên stack.
 *
 *          ═══ MISSING-KEY SEMANTICS ═══
 *          Mọi getter đều có cờ `found`:
 *            - key tồn tại   → ESP_OK, *found = true,  dữ liệu chép vào dst
 *            - key không có  → ESP_OK, *found = false, dst KHÔNG bị chạm
 *            - lỗi provider  → ESP_ERR_*, *found = false
 *          Caller giữ default của mình khi key không tồn tại — platform
 *          KHÔNG tự quyết định default.
 *
 *          ═══ ERROR MAPPING ═══
 *          Platform map một số lỗi NVS sang mã chung để caller không cần
 *          include nvs.h / biết chi tiết ESP-IDF:
 *            - ESP_ERR_NVS_NOT_FOUND      → ESP_ERR_NOT_FOUND
 *              (namespace chưa từng tạo, dùng ở platform_nvs_open)
 *            - ESP_ERR_NVS_INVALID_LENGTH → ESP_ERR_INVALID_SIZE
 *              (chuỗi lưu trữ không vừa bộ đệm caller)
 *          Các lỗi NVS khác giữ nguyên giá trị ESP_ERR_NVS_*.
 *
 *          ═══ KHÔNG LOG SECRET ═══
 *          Platform chỉ log namespace / key / error code — không bao giờ log
 *          value của chuỗi (có thể là password của caller).
 *
 *          ═══ ĐỘC LẬP ═══
 *          Component này KHÔNG biết: Config_t, WifiProfile_t, CallBox,
 *          namespace "callbox", bất kỳ key cụ thể nào, MQTT, Wi-Fi, SNTP,
 *          BSP, business. Mọi policy (namespace, key, schema, migration)
 *          là của caller. Cũng không phụ thuộc platform_time — hai platform
 *          service là peers.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#ifndef PLATFORM_NVS_H
#define PLATFORM_NVS_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Namespace session mở một lần, dùng nhiều thao tác rồi đóng (xem header). */
typedef struct {
    void *handle;   /**< nvs_handle_t ESP-IDF (được dấu để main không include nvs.h) */
} platform_nvs_handle_t;

/**
 * @brief  Khởi tạo NVS flash (gọi một lần khi boot).
 *
 *         Recovery: nếu partition bị hỏng (ESP_ERR_NVS_NO_FREE_PAGES /
 *         ESP_ERR_NVS_NEW_VERSION_FOUND) thì erase toàn bộ partition và
 *         init lại. Mọi lỗi khác được trả nguyên về caller — KHÔNG tự erase.
 *
 * @return ESP_OK                    sẵn sàng
 * @return ESP_ERR_NVS_*             lỗi từ ESP-IDF (không phải lỗi partition)
 */
esp_err_t platform_nvs_init(void);

/**
 * @brief  Mở một namespace (1 handle, nhiều thao tác, đóng sau cùng).
 *
 * @param  out_handle  Handle kết quả (caller khai báo trên stack, không cấp phát).
 * @param  ns_name     Tên namespace (do caller chọn, policy của caller).
 * @param  read_only   true = chỉ đọc; false = đọc/ghi.
 * @return ESP_OK                mở thành công
 * @return ESP_ERR_INVALID_ARG   out_handle / ns_name NULL
 * @return ESP_ERR_NOT_FOUND     namespace chưa từng tồn tại (boot lần đầu)
 * @return ESP_ERR_NVS_*         lỗi khác từ ESP-IDF
 */
esp_err_t platform_nvs_open(platform_nvs_handle_t *out_handle,
                            const char *ns_name, bool read_only);

/**
 * @brief  Đóng session đã mở bằng platform_nvs_open.
 *
 * @param  handle  Handle cần đóng.
 */
void platform_nvs_close(platform_nvs_handle_t *handle);

/**
 * @brief  Commit toàn bộ thay đổi từ lúc mở (dữ liệu thật sự xuống flash).
 *
 * @param  handle  Handle đã mở.
 * @return ESP_OK          commit thành công
 * @return ESP_ERR_NVS_*   lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_commit(platform_nvs_handle_t *handle);

/**
 * @brief  Xóa TOÀN BỘ partition NVS (factory reset mức flash).
 *
 * @return ESP_OK            xóa thành công
 * @return ESP_ERR_NVS_*     lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_erase_all(void);

/**
 * @brief  Đọc chuỗi; key không tồn tại → ESP_OK, *found = false.
 *
 * @param  handle  Handle đã mở.
 * @param  key     Tên key (policy của caller).
 * @param  dst     Bộ đệm đích.
 * @param  size    Kích thước bộ đệm đích (gồm null terminator).
 * @param  found   [out] true nếu key tồn tại. Có thể NULL.
 * @return ESP_OK                 đọc xong (dst giữ nguyên nếu !*found)
 * @return ESP_ERR_INVALID_ARG    tham số không hợp lệ
 * @return ESP_ERR_INVALID_SIZE   chuỗi lưu trữ không vừa bộ đệm
 * @return ESP_ERR_NVS_*          lỗi khác từ ESP-IDF
 */
esp_err_t platform_nvs_get_string(platform_nvs_handle_t *handle, const char *key,
                                  char *dst, size_t size, bool *found);

/**
 * @brief  Ghi chuỗi (phải commit mới xuống flash).
 *
 * @param  handle  Handle đã mở (read/write).
 * @param  key     Tên key (policy của caller).
 * @param  value   Chuỗi kết thúc null.
 * @return ESP_OK                ghi thành công (chưa commit)
 * @return ESP_ERR_INVALID_ARG   tham số không hợp lệ
 * @return ESP_ERR_NVS_*         lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_set_string(platform_nvs_handle_t *handle, const char *key,
                                  const char *value);

/**
 * @brief  Đọc u8; key không tồn tại → ESP_OK, *found = false.
 *
 * @param  handle  Handle đã mở.
 * @param  key     Tên key (policy của caller).
 * @param  value   [out] Giá trị đọc được.
 * @param  found   [out] true nếu key tồn tại. Có thể NULL.
 * @return ESP_OK                đọc xong (value giữ nguyên nếu !*found)
 * @return ESP_ERR_INVALID_ARG   tham số không hợp lệ
 * @return ESP_ERR_NVS_*         lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_get_u8(platform_nvs_handle_t *handle, const char *key,
                              uint8_t *value, bool *found);

/**
 * @brief  Ghi u8 (phải commit mới xuống flash).
 *
 * @param  handle  Handle đã mở (read/write).
 * @param  key     Tên key (policy của caller).
 * @param  value   Giá trị cần ghi.
 * @return ESP_OK                ghi thành công (chưa commit)
 * @return ESP_ERR_INVALID_ARG   tham số không hợp lệ
 * @return ESP_ERR_NVS_*         lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_set_u8(platform_nvs_handle_t *handle, const char *key,
                              uint8_t value);

/**
 * @brief  Đọc u16; key không tồn tại → ESP_OK, *found = false.
 *
 * @param  handle  Handle đã mở.
 * @param  key     Tên key (policy của caller).
 * @param  value   [out] Giá trị đọc được.
 * @param  found   [out] true nếu key tồn tại. Có thể NULL.
 * @return ESP_OK                đọc xong (value giữ nguyên nếu !*found)
 * @return ESP_ERR_INVALID_ARG   tham số không hợp lệ
 * @return ESP_ERR_NVS_*         lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_get_u16(platform_nvs_handle_t *handle, const char *key,
                               uint16_t *value, bool *found);

/**
 * @brief  Ghi u16 (phải commit mới xuống flash).
 *
 * @param  handle  Handle đã mở (read/write).
 * @param  key     Tên key (policy của caller).
 * @param  value   Giá trị cần ghi.
 * @return ESP_OK                ghi thành công (chưa commit)
 * @return ESP_ERR_INVALID_ARG   tham số không hợp lệ
 * @return ESP_ERR_NVS_*         lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_set_u16(platform_nvs_handle_t *handle, const char *key,
                               uint16_t value);

/**
 * @brief  Đọc u32; key không tồn tại → ESP_OK, *found = false.
 *
 * @param  handle  Handle đã mở.
 * @param  key     Tên key (policy của caller).
 * @param  value   [out] Giá trị đọc được.
 * @param  found   [out] true nếu key tồn tại. Có thể NULL.
 * @return ESP_OK                đọc xong (value giữ nguyên nếu !*found)
 * @return ESP_ERR_INVALID_ARG   tham số không hợp lệ
 * @return ESP_ERR_NVS_*         lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_get_u32(platform_nvs_handle_t *handle, const char *key,
                               uint32_t *value, bool *found);

/**
 * @brief  Ghi u32 (phải commit mới xuống flash).
 *
 * @param  handle  Handle đã mở (read/write).
 * @param  key     Tên key (policy của caller).
 * @param  value   Giá trị cần ghi.
 * @return ESP_OK                ghi thành công (chưa commit)
 * @return ESP_ERR_INVALID_ARG   tham số không hợp lệ
 * @return ESP_ERR_NVS_*         lỗi từ ESP-IDF
 */
esp_err_t platform_nvs_set_u32(platform_nvs_handle_t *handle, const char *key,
                               uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_NVS_H */
