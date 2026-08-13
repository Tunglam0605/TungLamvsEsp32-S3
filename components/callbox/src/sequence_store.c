/**
 * @file    sequence_store.c
 * @brief   Triển khai persistence cho seq_num (high-watermark tin nhắn MQTT).
 *
 *          Logic được chuyển gần như nguyên từ nvs_load_seq_num /
 *          nvs_save_seq_num (nvs_storage.c trước Phase E.2.1) — chỉ thay
 *          literal key bằng CALLBOX_STORAGE_* constants và bỏ các helper
 *          không liên quan. KHÔNG rewrite behavior.
 *
 *          ═══ LOAD SEMANTICS ═══
 *          - key thiếu thật sự → *sequence = 0, trả ESP_OK (first boot).
 *          - lỗi đọc/type/corruption → trả nguyên lỗi; tuyệt đối không giả làm
 *            first boot vì có thể làm tái sử dụng sequence đã phát.
 *          - save: ESP_OK CHỈ khi cả set lẫn commit thành công.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     sequence_store.h — API
 * @see     callbox_storage_schema.h — schema constants (product internal)
 */
#include "sequence_store.h"
#include "callbox_storage_schema.h"
#include "platform_nvs.h"
#include "esp_log.h"

static const char *TAG = "SEQ_STORE";

esp_err_t sequence_store_save(uint32_t sequence)
{
    platform_nvs_handle_t handle;
    esp_err_t err = platform_nvs_open(&handle, CALLBOX_STORAGE_NAMESPACE, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    /* Ghi u32 seq_num rồi commit để chắc chắn ghi xuống flash */
    err = platform_nvs_set_u32(&handle, CALLBOX_STORAGE_SEQ_KEY, sequence);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting seq_num: %s", esp_err_to_name(err));
    } else {
        err = platform_nvs_commit(&handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Saved seq_num=%lu", sequence);
        } else {
            ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        }
    }

    platform_nvs_close(&handle);
    return err;
}

esp_err_t sequence_store_load(uint32_t *sequence)
{
    if (!sequence) return ESP_ERR_INVALID_ARG;

    platform_nvs_handle_t handle;
    esp_err_t err = platform_nvs_open(&handle, CALLBOX_STORAGE_NAMESPACE, true);
    if (err == ESP_ERR_NOT_FOUND) {
        /* Namespace chưa tồn tại là first boot hợp lệ. */
        *sequence = 0;
        ESP_LOGW(TAG, "Sequence namespace not found, using 0");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    /* Provider phân biệt key thiếu bằng `found=false`; các lỗi đọc/type/corrupt
     * vẫn trả mã lỗi. Chỉ key thực sự chưa tồn tại mới được bắt đầu từ 0. */
    bool found = false;
    err = platform_nvs_get_u32(&handle, CALLBOX_STORAGE_SEQ_KEY, sequence, &found);
    if (err == ESP_OK && found) {
        ESP_LOGI(TAG, "Loaded seq_num=%lu", *sequence);
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "No seq_num found in NVS, using 0");
        *sequence = 0;
    } else {
        ESP_LOGE(TAG, "Error reading seq_num: %s", esp_err_to_name(err));
    }

    platform_nvs_close(&handle);
    return err;
}
