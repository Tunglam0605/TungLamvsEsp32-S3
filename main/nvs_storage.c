/**
 * @file    nvs_storage.c
 * @brief   Triển khai lưu/đọc cấu hình và seq_num bằng NVS (namespace "callbox").
 *
 *          Module này là tầng "bền" (persistent) của ứng dụng: toàn bộ cấu
 *          hình người dùng (Wi-Fi, MQTT, ID callbox, profile WiFi) được viết
 *          vào NVS flash để giữ qua reset/mất điện.
 *
 *          ═══ QUY ƯỚC KEY ═══
 *          ┌────────────────┬──────────────────────────────────────┐
 *          │ wifi_ssid/pass │ Mạng Wi-Fi chính đang dùng           │
 *          │ wifiX_ssid/pass│ 5 profile nhớ (wifi0..wifi4)        │
 *          │ mqtt_broker/port│ Địa chỉ + cổng broker              │
 *          │ callbox_id     │ ID logic của thiết bị                │
 *          │ web_pass       │ Mật khẩu portal                      │
 *          │ seq_num        │ Số thứ tự tin nhắn (u32)             │
 *          └────────────────┴──────────────────────────────────────┘
 *
 *          Warning: gọi nvs_flash_init() một lần duy nhất; không gọi lại
 *          trong task — module này xử lý tại app_main.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     nvs_storage.h — API
 * @see     queues.h — cấu trúc Config_t
 * @see     config_portal.c — gọi nvs_save_config khi lưu từ web
 */
#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "NVS_STORAGE";
#define NVS_NAMESPACE "callbox"
#define NVS_SEQ_KEY "seq_num"
#define NVS_WIFI_SSID_KEY "wifi_ssid"
#define NVS_WIFI_PASS_KEY "wifi_pass"
#define NVS_MQTT_BROKER_KEY "mqtt_broker"
#define NVS_MQTT_PORT_KEY "mqtt_port"
#define NVS_MQTT_TRANSPORT_KEY "mqtt_tls"
#define NVS_MQTT_USER_KEY "mqtt_user"
#define NVS_MQTT_PASS_KEY "mqtt_pass"
#define NVS_CALLBOX_ID_KEY "callbox_id"
#define NVS_WEB_PASS_KEY "web_pass"
#define NVS_WIFI_COUNT_KEY "wifi_count"
#define NVS_WIFI_DHCP_KEY "wifi_dhcp"
#define NVS_WIFI_IP_KEY "wifi_ip"
#define NVS_WIFI_NETMASK_KEY "wifi_mask"
#define NVS_WIFI_GATEWAY_KEY "wifi_gw"
#define NVS_WIFI_DNS_KEY "wifi_dns"
#define NVS_SNTP_PRIMARY_KEY "sntp_primary"
#define NVS_SNTP_FALLBACK_KEY "sntp_fallback"

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

/* Đọc chuỗi NVS; nếu key chưa tồn tại thì trả về ESP_OK (giữ giá trị mặc định) */
static esp_err_t nvs_get_string_if_present(nvs_handle_t handle,
                                           const char *key,
                                           char *value,
                                           size_t value_size)
{
    size_t required = value_size;
    esp_err_t err = nvs_get_str(handle, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "NVS string too long for key '%s'", key);
    }
    return err;
}

esp_err_t nvs_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    /* Nếu phân vùng NVS bị hỏng/đầy → xóa sạch và khởi tạo lại */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t nvs_save_seq_num(uint32_t seq)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    /* Ghi u32 seq_num rồi commit để chắc chắn ghi xuống flash */
    err = nvs_set_u32(handle, NVS_SEQ_KEY, seq);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting seq_num: %s", esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Saved seq_num=%lu", seq);
        } else {
            ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        }
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_load_seq_num(uint32_t *seq)
{
    if (!seq) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        *seq = 0;
        return err;
    }

    /* Đọc u32 seq_num; nếu chưa có → dùng 0 */
    err = nvs_get_u32(handle, NVS_SEQ_KEY, seq);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No seq_num found in NVS, using 0");
        *seq = 0;
    } else {
        ESP_LOGI(TAG, "Loaded seq_num=%lu", *seq);
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_load_config(Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved callbox configuration; using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    /* Đọc lần lượt từng key; lỗi đầu tiên được giữ trong first_error */
    esp_err_t first_error = ESP_OK;
    esp_err_t item_err;
    bool commit_web_password = false;
    item_err = nvs_get_string_if_present(handle, NVS_WIFI_SSID_KEY,
                                         config->wifi_ssid, sizeof(config->wifi_ssid));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_WIFI_PASS_KEY,
                                         config->wifi_pass, sizeof(config->wifi_pass));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_WIFI_IP_KEY,
                                         config->wifi_ip, sizeof(config->wifi_ip));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_WIFI_NETMASK_KEY,
                                         config->wifi_netmask, sizeof(config->wifi_netmask));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_WIFI_GATEWAY_KEY,
                                         config->wifi_gateway, sizeof(config->wifi_gateway));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_WIFI_DNS_KEY,
                                         config->wifi_dns, sizeof(config->wifi_dns));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_SNTP_PRIMARY_KEY,
                                         config->sntp_primary, sizeof(config->sntp_primary));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_SNTP_FALLBACK_KEY,
                                         config->sntp_fallback, sizeof(config->sntp_fallback));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    uint8_t dhcp = config->wifi_dhcp ? 1 : 0;
    item_err = nvs_get_u8(handle, NVS_WIFI_DHCP_KEY, &dhcp);
    if (item_err == ESP_OK) {
        config->wifi_dhcp = dhcp != 0;
    } else if (item_err != ESP_ERR_NVS_NOT_FOUND && first_error == ESP_OK) {
        first_error = item_err;
    }
    item_err = nvs_get_string_if_present(handle, NVS_MQTT_BROKER_KEY,
                                         config->mqtt_broker, sizeof(config->mqtt_broker));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_MQTT_USER_KEY,
                                         config->mqtt_user, sizeof(config->mqtt_user));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_MQTT_PASS_KEY,
                                         config->mqtt_pass, sizeof(config->mqtt_pass));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    item_err = nvs_get_string_if_present(handle, NVS_CALLBOX_ID_KEY,
                                         config->callbox_id, sizeof(config->callbox_id));
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;

    /* web_pass: nếu chưa tồn tại → ghi mặc định ngay (migrate bản cũ) */
    size_t web_password_len = sizeof(config->web_password);
    item_err = nvs_get_str(handle, NVS_WEB_PASS_KEY, config->web_password, &web_password_len);
    if (item_err == ESP_ERR_NVS_NOT_FOUND) {
        item_err = nvs_set_str(handle, NVS_WEB_PASS_KEY, config->web_password);
        commit_web_password = item_err == ESP_OK;
    }
    if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;

    uint16_t port = config->mqtt_port;
    item_err = nvs_get_u16(handle, NVS_MQTT_PORT_KEY, &port);
    if (item_err == ESP_OK) {
        config->mqtt_port = port;
    } else if (item_err != ESP_ERR_NVS_NOT_FOUND && first_error == ESP_OK) {
        first_error = item_err;
    }

    /* Missing key means this is an older installation; preserve its original
     * plaintext broker behavior by keeping the TCP default from Config_t. */
    uint8_t mqtt_tls = config->mqtt_transport == MQTT_TRANSPORT_TLS ? 1U : 0U;
    item_err = nvs_get_u8(handle, NVS_MQTT_TRANSPORT_KEY, &mqtt_tls);
    if (item_err == ESP_OK) {
        config->mqtt_transport = mqtt_tls ? MQTT_TRANSPORT_TLS : MQTT_TRANSPORT_TCP;
    } else if (item_err != ESP_ERR_NVS_NOT_FOUND && first_error == ESP_OK) {
        first_error = item_err;
    }

    /* Đọc danh sách profile: wifi_count + wifi{i}_ssid/wifi{i}_pass */
    uint8_t profile_count = config->wifi_profile_count;
    item_err = nvs_get_u8(handle, NVS_WIFI_COUNT_KEY, &profile_count);
    if (item_err == ESP_OK) {
        if (profile_count > MAX_WIFI_PROFILES) profile_count = MAX_WIFI_PROFILES;
        config->wifi_profile_count = profile_count;
        for (uint8_t i = 0; i < profile_count; i++) {
            char key[16];
            snprintf(key, sizeof(key), "wifi%u_ssid", (unsigned)i);
            item_err = nvs_get_string_if_present(handle, key,
                                                 config->wifi_profiles[i].ssid,
                                                 sizeof(config->wifi_profiles[i].ssid));
            if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
            snprintf(key, sizeof(key), "wifi%u_pass", (unsigned)i);
            item_err = nvs_get_string_if_present(handle, key,
                                                 config->wifi_profiles[i].password,
                                                 sizeof(config->wifi_profiles[i].password));
            if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
        }
    } else if (item_err != ESP_ERR_NVS_NOT_FOUND && first_error == ESP_OK) {
        first_error = item_err;
    }

    /* Nếu chưa có profile nhưng có wifi_ssid → tạo profile từ mạng cũ */
    if (config->wifi_profile_count == 0 && config->wifi_ssid[0]) {
        config_add_wifi_profile(config, config->wifi_ssid, config->wifi_pass);
    }

    if (commit_web_password) {
        item_err = nvs_commit(handle);
        if (item_err != ESP_OK && first_error == ESP_OK) first_error = item_err;
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "Configuration loaded: callbox=%s broker=%s:%u WiFi=%s",
             config->callbox_id, config->mqtt_broker, config->mqtt_port,
             config->wifi_ssid[0] ? config->wifi_ssid : "<not configured>");
    return first_error;
}

esp_err_t nvs_save_config(const Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    /* Ghi lần lượt mọi key; nếu 1 key lỗi thì dừng chuỗi ghi (chuỗi if) */
    err = nvs_set_str(handle, NVS_WIFI_SSID_KEY, config->wifi_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_WIFI_PASS_KEY, config->wifi_pass);
    if (err == ESP_OK) err = nvs_set_u8(handle, NVS_WIFI_DHCP_KEY, config->wifi_dhcp ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_WIFI_IP_KEY, config->wifi_ip);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_WIFI_NETMASK_KEY, config->wifi_netmask);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_WIFI_GATEWAY_KEY, config->wifi_gateway);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_WIFI_DNS_KEY, config->wifi_dns);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_SNTP_PRIMARY_KEY, config->sntp_primary);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_SNTP_FALLBACK_KEY, config->sntp_fallback);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_MQTT_BROKER_KEY, config->mqtt_broker);
    if (err == ESP_OK) err = nvs_set_u16(handle, NVS_MQTT_PORT_KEY, config->mqtt_port);
    if (err == ESP_OK) err = nvs_set_u8(handle, NVS_MQTT_TRANSPORT_KEY,
                                        config->mqtt_transport == MQTT_TRANSPORT_TLS ? 1U : 0U);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_MQTT_USER_KEY, config->mqtt_user);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_MQTT_PASS_KEY, config->mqtt_pass);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_CALLBOX_ID_KEY, config->callbox_id);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_WEB_PASS_KEY, config->web_password);
    if (err == ESP_OK) {
        /* Ghi từng profile mạng nhớ: wifi{i}_ssid / wifi{i}_pass */
        uint8_t count = config->wifi_profile_count > MAX_WIFI_PROFILES ? MAX_WIFI_PROFILES : config->wifi_profile_count;
        err = nvs_set_u8(handle, NVS_WIFI_COUNT_KEY, count);
        for (uint8_t i = 0; err == ESP_OK && i < count; i++) {
            char key[16];
            snprintf(key, sizeof(key), "wifi%u_ssid", (unsigned)i);
            err = nvs_set_str(handle, key, config->wifi_profiles[i].ssid);
            snprintf(key, sizeof(key), "wifi%u_pass", (unsigned)i);
            if (err == ESP_OK) err = nvs_set_str(handle, key, config->wifi_profiles[i].password);
        }
    }
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Configuration saved: callbox=%s broker=%s:%u",
                 config->callbox_id, config->mqtt_broker, config->mqtt_port);
    }
    return err;
}

esp_err_t nvs_storage_erase_all(void)
{
    ESP_LOGW(TAG, "Erasing all NVS data");
    return nvs_flash_erase();
}
