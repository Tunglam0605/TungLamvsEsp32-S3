/**
 * @file    platform_nvs.c
 * @brief   Triển khai dịch vụ NVS nền tảng (lifecycle + typed access).
 *
 *          ═══ PHẠM VI ═══
 *          Component này là tầng duy nhất gọi trực tiếp API ESP-IDF NVS:
 *          nvs_flash_init / nvs_flash_erase / nvs_open / nvs_close /
 *          nvs_commit / nvs_get_* / nvs_set_*. Caller (product persistence
 *          adapter) chỉ truyền namespace + key + dữ liệu qua platform_nvs_*.
 *
 *          ═══ HANDLE DESIGN ═══
 *          platform_nvs_handle_t bọc nvs_handle_t (số nguyên 32-bit của
 *          ESP-IDF) trong struct nhỏ, deterministic — không cấp phát heap,
 *          không opaque allocator. Caller khai báo trên stack:
 *              platform_nvs_handle_t h;
 *              platform_nvs_open(&h, ns, false);
 *              platform_nvs_set_string(&h, k, v);
 *              platform_nvs_commit(&h);
 *              platform_nvs_close(&h);
 *          Mỗi session = một handle: mở 1 lần, nhiều SET, COMMIT 1 lần,
 *          đóng 1 lần — giữ nguyên atomicity và giảm flash wear.
 *
 *          ═══ MISSING-KEY SEMANTICS ═══
 *          Getter trả ESP_OK + *found = false khi key không tồn tại. Caller
 *          quyết định default; platform không tự cung cấp default nào.
 *
 *          ═══ KHÔNG LOG SECRET ═══
 *          Chỉ log namespace / key / error code. KHÔNG log value chuỗi.
 *
 *          ═══ ĐỘC LẬP ═══
 *          Không include/biết gì về: Config_t, WifiProfile_t, CallBox,
 *          namespace "callbox", key cụ thể, MQTT, Wi-Fi, SNTP, BSP.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#include "platform_nvs.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "PLATFORM_NVS";

/* Map mã lỗi NVS sang mã chung để caller không cần biết ESP_ERR_NVS_*:
 *   - namespace/key chưa tồn tại  → ESP_ERR_NOT_FOUND (generic)
 *   - chuỗi lưu trữ không vừa bộ đệm → ESP_ERR_INVALID_SIZE
 *   - các lỗi khác giữ nguyên ESP_ERR_NVS_* */
static esp_err_t platform_nvs_map_error(esp_err_t err)
{
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t platform_nvs_init(void)
{
    /* Legacy NVS is migration input. Never erase it implicitly. */
    return nvs_flash_init();
}

esp_err_t platform_nvs_init_partition(const char *partition_name)
{
    if (!partition_name) return ESP_ERR_INVALID_ARG;

    esp_err_t err;
#ifdef CONFIG_NVS_ENCRYPTION
    /* Custom NVS partitions are not automatically encrypted by
     * nvs_flash_init_partition(). Reuse the active default security scheme
     * (HMAC in CallBox production) and the same derived XTS key material. */
    nvs_sec_scheme_t *scheme = nvs_flash_get_default_security_scheme();
    if (!scheme) {
        ESP_LOGE(TAG, "NVS encryption is enabled but no security scheme is active");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_sec_cfg_t cfg = {0};
    err = nvs_flash_read_security_cfg_v2(scheme, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS security configuration unavailable: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = nvs_flash_secure_init_partition(partition_name, &cfg);
#else
    err = nvs_flash_init_partition(partition_name);
#endif

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS partition '%s' init failed: %s", partition_name,
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t platform_nvs_open(platform_nvs_handle_t *out_handle,
                            const char *ns_name, bool read_only)
{
    if (!out_handle || !ns_name) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_open_mode_t mode = read_only ? NVS_READONLY : NVS_READWRITE;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns_name, mode, &handle);
    if (err != ESP_OK) {
        /* NOT_FOUND = namespace chưa tồn tại (lần boot đầu) — trạng thái bình
         * thường, không log lỗi. Các lỗi thật (hỏng partition...) mới log. */
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "nvs_open('%s') failed: %s", ns_name, esp_err_to_name(err));
        }
        return platform_nvs_map_error(err);
    }
    out_handle->handle = (void *)(uintptr_t)handle;
    return ESP_OK;
}

esp_err_t platform_nvs_open_partition(platform_nvs_handle_t *out_handle,
                                      const char *partition_name,
                                      const char *ns_name, bool read_only)
{
    if (!out_handle || !partition_name || !ns_name) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open_from_partition(
        partition_name, ns_name, read_only ? NVS_READONLY : NVS_READWRITE, &handle);
    if (err != ESP_OK) return platform_nvs_map_error(err);
    out_handle->handle = (void *)(uintptr_t)handle;
    return ESP_OK;
}

void platform_nvs_close(platform_nvs_handle_t *handle)
{
    if (!handle) {
        return;
    }
    nvs_handle_t raw = (nvs_handle_t)(uintptr_t)handle->handle;
    nvs_close(raw);
    handle->handle = NULL;
}

esp_err_t platform_nvs_commit(platform_nvs_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return platform_nvs_map_error(nvs_commit((nvs_handle_t)(uintptr_t)handle->handle));
}

esp_err_t platform_nvs_erase_all(void)
{
    return platform_nvs_map_error(nvs_flash_erase());
}

esp_err_t platform_nvs_get_string(platform_nvs_handle_t *handle, const char *key,
                                  char *dst, size_t size, bool *found)
{
    if (!handle || !key || !dst || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (found) {
        *found = false;
    }
    size_t required = size;
    esp_err_t err = nvs_get_str((nvs_handle_t)(uintptr_t)handle->handle,
                                key, dst, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "NVS string too long for key '%s'", key);
        return ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK && found) {
        *found = true;
    }
    return err;
}

esp_err_t platform_nvs_set_string(platform_nvs_handle_t *handle, const char *key,
                                  const char *value)
{
    if (!handle || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    return platform_nvs_map_error(
        nvs_set_str((nvs_handle_t)(uintptr_t)handle->handle, key, value));
}

esp_err_t platform_nvs_get_u8(platform_nvs_handle_t *handle, const char *key,
                              uint8_t *value, bool *found)
{
    if (!handle || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (found) {
        *found = false;
    }
    esp_err_t err = nvs_get_u8((nvs_handle_t)(uintptr_t)handle->handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK && found) {
        *found = true;
    }
    return err;
}

esp_err_t platform_nvs_set_u8(platform_nvs_handle_t *handle, const char *key,
                              uint8_t value)
{
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    return platform_nvs_map_error(
        nvs_set_u8((nvs_handle_t)(uintptr_t)handle->handle, key, value));
}

esp_err_t platform_nvs_get_u16(platform_nvs_handle_t *handle, const char *key,
                               uint16_t *value, bool *found)
{
    if (!handle || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (found) {
        *found = false;
    }
    esp_err_t err = nvs_get_u16((nvs_handle_t)(uintptr_t)handle->handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK && found) {
        *found = true;
    }
    return err;
}

esp_err_t platform_nvs_set_u16(platform_nvs_handle_t *handle, const char *key,
                               uint16_t value)
{
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    return platform_nvs_map_error(
        nvs_set_u16((nvs_handle_t)(uintptr_t)handle->handle, key, value));
}

esp_err_t platform_nvs_get_u32(platform_nvs_handle_t *handle, const char *key,
                               uint32_t *value, bool *found)
{
    if (!handle || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (found) {
        *found = false;
    }
    esp_err_t err = nvs_get_u32((nvs_handle_t)(uintptr_t)handle->handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK && found) {
        *found = true;
    }
    return err;
}

esp_err_t platform_nvs_set_u32(platform_nvs_handle_t *handle, const char *key,
                               uint32_t value)
{
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    return platform_nvs_map_error(
        nvs_set_u32((nvs_handle_t)(uintptr_t)handle->handle, key, value));
}
