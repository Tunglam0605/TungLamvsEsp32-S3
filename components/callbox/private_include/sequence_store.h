/**
 * @file    sequence_store.h
 * @brief   Persistence riêng cho seq_num (high-watermark tin nhắn MQTT).
 *
 *          Module này chỉ lưu/đọc một giá trị u32 — KHÔNG biết Config_t,
 *          Wi-Fi, MQTT hay bất kỳ key cấu hình nào. Schema (namespace "callbox"
 *          + key "seq_num") nằm trong callbox_storage_schema.h.
 *
 *          ═══ SEMANTICS ═══
 *          - sequence_store_load(): key thiếu → *sequence = 0 và trả ESP_OK;
 *            lỗi đọc/type/corrupt được trả nguyên để caller fail-safe.
 *          - sequence_store_save(): mở → set u32 → commit → đóng. Trả ESP_OK
 *            CHỈ khi commit thành công (dữ liệu thực sự xuống flash).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     sequence_service.c — người dùng duy nhất của module này
 * @see     callbox_storage_schema.h — schema constants (product internal)
 */
#ifndef SEQUENCE_STORE_H
#define SEQUENCE_STORE_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Nạp high-watermark đã lưu (số thứ tự tin nhắn lớn nhất).
 *
 * @param  sequence  [out] Giá trị đọc được. Key thiếu → 0.
 * @return ESP_OK            đọc xong (key thiếu cũng trả ESP_OK)
 * @return ESP_ERR_INVALID_ARG  sequence NULL
 * @return ESP_ERR_NVS_*        lỗi từ provider
 */
esp_err_t sequence_store_load(uint32_t *sequence);

/**
 * @brief  Lưu high-watermark (chỉ lưu giá trị đã vượt qua mọi kiểm tra).
 *
 * @param  sequence  Giá trị cần lưu.
 * @return ESP_OK            ghi + commit thành công
 * @return ESP_ERR_NVS_*     lỗi từ provider (không ghi được xuống flash)
 */
esp_err_t sequence_store_save(uint32_t sequence);

#ifdef __cplusplus
}
#endif

#endif /* SEQUENCE_STORE_H */
