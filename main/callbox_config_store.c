/**
 * @file    callbox_config_store.c
 * @brief   Triển khai CallBox configuration persistence + migration.
 *
 *          Module này là tầng product adapter cho cấu hình: sở hữu namespace
 *          "callbox", schema/key cấu hình, chính sách profile Wi-Fi và mọi
 *          migration (web_pass, legacy WiFi, mqtt_tls...). Mọi provider
 *          mechanics đi qua platform_nvs.
 *
 *          ═══ MIGRATION (GIỮ NGUYÊN) ═══
 *          - key thiếu → giữ default caller đã đặt trong Config_t
 *          - web_pass thiếu → ghi config->web_password xuống + commit
 *          - mqtt_tls thiếu → giữ config->mqtt_transport (TCP/TLS)
 *          - wifi_profile_count == 0 nhưng wifi_ssid[0] → tạo profile từ mạng cũ
 *          - wifi_count > MAX_WIFI_PROFILES → clamp về MAX_WIFI_PROFILES
 *
 *          ═══ TRANSACTION SAVE ═══
 *          Mở 1 lần → set mọi key → commit đúng 1 lần nếu mọi set thành công
 *          → đóng 1 lần. Không commit từng field, không lazy save.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     callbox_config_store.h — API
 * @see     callbox_storage_schema.h — schema constants (product internal)
 * @see     config_portal.c — gọi save khi lưu từ web
 */
#include "callbox_config_store.h"
#include "callbox_storage_schema.h"
#include "platform_nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CONFIG_STORE";

/* Hàm phụ: chép chuỗi dài vào bộ đệm đích, đảm bảo kết thúc '\0' */
static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    strncpy(dst, src ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void config_add_wifi_profile(Config_t *config, const char *ssid, const char *password)
{
    if (!config || !ssid || !ssid[0]) return;

    size_t count = config->wifi_profile_count;
    if (count > MAX_WIFI_PROFILES) count = MAX_WIFI_PROFILES;
    size_t index = count;
    for (size_t i = 0; i < index; i++) {
        /* Nếu SSID đã tồn tại → dùng ngay vị trí cũ (đề phòng trùng) */
        if (strcmp(config->wifi_profiles[i].ssid, ssid) == 0) {
            index = i;
            break;
        }
    }

    if (index == count) {
        /* Chưa tồn tại: tăng số profile (nếu chưa đầy MAX) */
        if (count < MAX_WIFI_PROFILES) {
            count++;
            config->wifi_profile_count = (uint8_t)count;
        }
        index = count - 1;
    }

    /* Dịch các profile cũ sang phải để chèn mạng MỚI lên đầu (ưu tiên) */
    for (size_t i = index; i > 0; i--) {
        config->wifi_profiles[i] = config->wifi_profiles[i - 1];
    }
    copy_string(config->wifi_profiles[0].ssid, sizeof(config->wifi_profiles[0].ssid), ssid);
    copy_string(config->wifi_profiles[0].password, sizeof(config->wifi_profiles[0].password), password);
    /* Luôn cập nhật mạng "đang dùng" bằng mạng mới thêm */
    copy_string(config->wifi_ssid, sizeof(config->wifi_ssid), ssid);
    copy_string(config->wifi_pass, sizeof(config->wifi_pass), password);
}

bool config_find_wifi_password(const Config_t *config, const char *ssid,
                               char *password, size_t password_size)
{
    if (!config || !ssid || !password || password_size == 0) return false;
    /* Duyệt các profile nhớ, trả mật khẩu nếu tìm thấy SSID */
    for (size_t i = 0; i < config->wifi_profile_count && i < MAX_WIFI_PROFILES; i++) {
        if (strcmp(config->wifi_profiles[i].ssid, ssid) == 0) {
            copy_string(password, password_size, config->wifi_profiles[i].password);
            return true;
        }
    }
    return false;
}

bool config_remove_wifi_profile(Config_t *config, const char *ssid)
{
    if (!config || !ssid || !ssid[0]) return false;
    const uint8_t count = config->wifi_profile_count > MAX_WIFI_PROFILES
                              ? MAX_WIFI_PROFILES : config->wifi_profile_count;
    for (uint8_t i = 0; i < count; ++i) {
        if (strcmp(config->wifi_profiles[i].ssid, ssid) != 0) continue;
        /* Dịch các profile sau vị trí xóa sang trái để lấp chỗ trống */
        for (uint8_t j = i; j + 1 < count; ++j) {
            config->wifi_profiles[j] = config->wifi_profiles[j + 1];
        }
        memset(&config->wifi_profiles[count - 1], 0, sizeof(config->wifi_profiles[0]));
        config->wifi_profile_count = count - 1;

        /* Cập nhật mạng "đang": dùng profile đầu (ưu tiên) hoặc xóa hẳn */
        if (config->wifi_profile_count) {
            copy_string(config->wifi_ssid, sizeof(config->wifi_ssid), config->wifi_profiles[0].ssid);
            copy_string(config->wifi_pass, sizeof(config->wifi_pass), config->wifi_profiles[0].password);
        } else {
            config->wifi_ssid[0] = '\0';
            config->wifi_pass[0] = '\0';
        }
        return true;
    }
    return false;
}

/* Đọc chuỗi NVS; nếu key chưa tồn tại thì trả về ESP_OK (giữ giá trị mặc định).
 * Mọi lỗi đọc (vd. chuỗi lưu trữ quá dài) được log và propagate — không cắt. */
static esp_err_t nvs_get_string_if_present(platform_nvs_handle_t *handle,
                                           const char *key,
                                           char *value,
                                           size_t value_size)
{
    bool found = false;
    esp_err_t err = platform_nvs_get_string(handle, key, value, value_size, &found);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read NVS string key '%s': %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t callbox_config_store_load(Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    platform_nvs_handle_t handle;
    esp_err_t err = platform_nvs_open(&handle, CALLBOX_STORAGE_NAMESPACE, false);
    if (err == ESP_ERR_NOT_FOUND) {
        /* Namespace chưa từng tạo (boot lần đầu) — dùng toàn bộ default.
         * Mã generic ESP_ERR_NOT_FOUND do platform_nvs map từ NVS_NOT_FOUND. */
        ESP_LOGI(TAG, "No saved callbox configuration; using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    /* Đọc lần lượt từng key; lỗi đầu tiên được giữ trong first_error */
    esp_err_t first_error = ESP_OK;
    esp_err_t item_err;
    bool commit_web_password = false;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_WIFI_SSID_KEY,
                                         config->wifi_ssid, sizeof(config->wifi_ssid));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_WIFI_PASS_KEY,
                                         config->wifi_pass, sizeof(config->wifi_pass));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_WIFI_IP_KEY,
                                         config->wifi_ip, sizeof(config->wifi_ip));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_WIFI_NETMASK_KEY,
                                         config->wifi_netmask, sizeof(config->wifi_netmask));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_WIFI_GATEWAY_KEY,
                                         config->wifi_gateway, sizeof(config->wifi_gateway));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_WIFI_DNS_KEY,
                                         config->wifi_dns, sizeof(config->wifi_dns));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_SNTP_PRIMARY_KEY,
                                         config->sntp_primary, sizeof(config->sntp_primary));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_SNTP_FALLBACK_KEY,
                                         config->sntp_fallback, sizeof(config->sntp_fallback));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    bool found_dhcp = false;
    uint8_t dhcp = config->wifi_dhcp ? 1 : 0;
    item_err = platform_nvs_get_u8(&handle, CALLBOX_STORAGE_WIFI_DHCP_KEY, &dhcp, &found_dhcp);
    if (found_dhcp) {
        config->wifi_dhcp = dhcp != 0;
    } else if (item_err != ESP_OK && first_error == ESP_OK) {
        first_error = item_err;
    }
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_MQTT_BROKER_KEY,
                                         config->mqtt_broker, sizeof(config->mqtt_broker));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_MQTT_USER_KEY,
                                         config->mqtt_user, sizeof(config->mqtt_user));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_MQTT_PASS_KEY,
                                         config->mqtt_pass, sizeof(config->mqtt_pass));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(&handle, CALLBOX_STORAGE_CALLBOX_ID_KEY,
                                         config->callbox_id, sizeof(config->callbox_id));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;

    /* web_pass: nếu key chưa tồn tại (ESP_OK + !found) → ghi mặc định ngay
     * (migrate bản cũ). Mặc định này do Config_t/caller cung cấp — không log.
     * Lỗi khác (vd. INVALID_SIZE) chỉ được ghi vào first_error, KHÔNG ghi đè. */
    size_t web_password_len = sizeof(config->web_password);
    bool found_web_pass = false;
    item_err = platform_nvs_get_string(&handle, CALLBOX_STORAGE_WEB_PASS_KEY,
                                       config->web_password, web_password_len, &found_web_pass);
    if (item_err == ESP_OK && !found_web_pass) {
        item_err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WEB_PASS_KEY, config->web_password);
        commit_web_password = item_err == ESP_OK;
    }
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;

    bool found_port = false;
    uint16_t port = config->mqtt_port;
    item_err = platform_nvs_get_u16(&handle, CALLBOX_STORAGE_MQTT_PORT_KEY, &port, &found_port);
    if (found_port) {
        config->mqtt_port = port;
    } else if (item_err != ESP_OK && first_error == ESP_OK) {
        first_error = item_err;
    }

    /* Key thiếu nghĩa là bản cài đặt cũ; giữ hành vi broker plaintext ban đầu
     * bằng cách giữ mặc định TCP từ Config_t. */
    bool found_mqtt_tls = false;
    uint8_t mqtt_tls = config->mqtt_transport == MQTT_TRANSPORT_TLS ? 1U : 0U;
    item_err = platform_nvs_get_u8(&handle, CALLBOX_STORAGE_MQTT_TRANSPORT_KEY, &mqtt_tls, &found_mqtt_tls);
    if (found_mqtt_tls) {
        config->mqtt_transport = mqtt_tls ? MQTT_TRANSPORT_TLS : MQTT_TRANSPORT_TCP;
    } else if (item_err != ESP_OK && first_error == ESP_OK) {
        first_error = item_err;
    }

    /* Đọc danh sách profile: wifi_count + wifi{i}_ssid/wifi{i}_pass */
    bool found_profile_count = false;
    uint8_t profile_count = config->wifi_profile_count;
    item_err = platform_nvs_get_u8(&handle, CALLBOX_STORAGE_WIFI_COUNT_KEY, &profile_count, &found_profile_count);
    if (found_profile_count) {
        if (profile_count > MAX_WIFI_PROFILES) profile_count = MAX_WIFI_PROFILES;
        config->wifi_profile_count = profile_count;
        for (uint8_t i = 0; i < profile_count; i++) {
            char key[16];
            snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                     (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_SSID);
            item_err = nvs_get_string_if_present(&handle, key,
                                                 config->wifi_profiles[i].ssid,
                                                 sizeof(config->wifi_profiles[i].ssid));
            if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
            snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                     (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_PASS);
            item_err = nvs_get_string_if_present(&handle, key,
                                                 config->wifi_profiles[i].password,
                                                 sizeof(config->wifi_profiles[i].password));
            if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
        }
    } else if (item_err != ESP_OK && first_error == ESP_OK) {
        first_error = item_err;
    }

    /* Nếu chưa có profile nhưng có wifi_ssid → tạo profile từ mạng cũ */
    if (config->wifi_profile_count == 0 && config->wifi_ssid[0]) {
        config_add_wifi_profile(config, config->wifi_ssid, config->wifi_pass);
    }

    if (commit_web_password) {
        item_err = platform_nvs_commit(&handle);
        if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    }
    platform_nvs_close(&handle);
    ESP_LOGI(TAG, "Configuration loaded: callbox=%s broker=%s:%u WiFi=%s",
             config->callbox_id, config->mqtt_broker, config->mqtt_port,
             config->wifi_ssid[0] ? config->wifi_ssid : "<not configured>");
    return first_error;
}

esp_err_t callbox_config_store_save(const Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    platform_nvs_handle_t handle;
    esp_err_t err = platform_nvs_open(&handle, CALLBOX_STORAGE_NAMESPACE, false);
    if (err != ESP_OK) return err;

    /* Mở 1 lần → ghi lần lượt mọi key; nếu 1 key lỗi thì dừng chuỗi ghi
     * (chuỗi if) → COMMIT duy nhất khi mọi SET thành công → đóng. */
    err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WIFI_SSID_KEY, config->wifi_ssid);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WIFI_PASS_KEY, config->wifi_pass);
    if (err == ESP_OK) err = platform_nvs_set_u8(&handle, CALLBOX_STORAGE_WIFI_DHCP_KEY, config->wifi_dhcp ? 1 : 0);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WIFI_IP_KEY, config->wifi_ip);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WIFI_NETMASK_KEY, config->wifi_netmask);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WIFI_GATEWAY_KEY, config->wifi_gateway);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WIFI_DNS_KEY, config->wifi_dns);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_SNTP_PRIMARY_KEY, config->sntp_primary);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_SNTP_FALLBACK_KEY, config->sntp_fallback);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_MQTT_BROKER_KEY, config->mqtt_broker);
    if (err == ESP_OK) err = platform_nvs_set_u16(&handle, CALLBOX_STORAGE_MQTT_PORT_KEY, config->mqtt_port);
    if (err == ESP_OK) err = platform_nvs_set_u8(&handle, CALLBOX_STORAGE_MQTT_TRANSPORT_KEY,
                                                 config->mqtt_transport == MQTT_TRANSPORT_TLS ? 1U : 0U);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_MQTT_USER_KEY, config->mqtt_user);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_MQTT_PASS_KEY, config->mqtt_pass);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_CALLBOX_ID_KEY, config->callbox_id);
    if (err == ESP_OK) err = platform_nvs_set_string(&handle, CALLBOX_STORAGE_WEB_PASS_KEY, config->web_password);
    if (err == ESP_OK) {
        /* Ghi từng profile mạng nhớ: wifi{i}_ssid / wifi{i}_pass */
        uint8_t count = config->wifi_profile_count > MAX_WIFI_PROFILES ? MAX_WIFI_PROFILES : config->wifi_profile_count;
        err = platform_nvs_set_u8(&handle, CALLBOX_STORAGE_WIFI_COUNT_KEY, count);
        for (uint8_t i = 0; err == ESP_OK && i < count; i++) {
            char key[16];
            snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                     (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_SSID);
            err = platform_nvs_set_string(&handle, key, config->wifi_profiles[i].ssid);
            snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                     (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_PASS);
            if (err == ESP_OK) err = platform_nvs_set_string(&handle, key, config->wifi_profiles[i].password);
        }
    }
    if (err == ESP_OK) err = platform_nvs_commit(&handle);

    platform_nvs_close(&handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Configuration saved: callbox=%s broker=%s:%u",
                 config->callbox_id, config->mqtt_broker, config->mqtt_port);
    }
    return err;
}
