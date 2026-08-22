#include "callbox_storage_migration.h"

#include <stdio.h>
#include <string.h>

#include "callbox_config.h"
#include "callbox_storage_schema.h"
#include "platform_nvs.h"

#define MIGRATION_NAMESPACE "schema"
#define MIGRATION_STATE_KEY "mig_state"
#define MIGRATION_VERSION_KEY "mig_ver"
#define MIGRATION_IN_PROGRESS 1U
#define MIGRATION_VERIFIED 2U
#define CALLBOX_STORAGE_SCHEMA_VERSION 1U
#define SEQUENCE_NAMESPACE "sequence"
#define SEQUENCE_HIGH_WATERMARK_KEY "high_watermark"

static const char *const s_string_keys[] = {
    CALLBOX_STORAGE_WIFI_SSID_KEY, CALLBOX_STORAGE_WIFI_PASS_KEY,
    CALLBOX_STORAGE_WIFI_IP_KEY, CALLBOX_STORAGE_WIFI_NETMASK_KEY,
    CALLBOX_STORAGE_WIFI_GATEWAY_KEY, CALLBOX_STORAGE_WIFI_DNS_KEY,
    CALLBOX_STORAGE_MQTT_BROKER_KEY, CALLBOX_STORAGE_MQTT_USER_KEY,
    CALLBOX_STORAGE_MQTT_PASS_KEY, CALLBOX_STORAGE_CALLBOX_ID_KEY,
    CALLBOX_STORAGE_WEB_PASS_KEY, CALLBOX_STORAGE_SNTP_PRIMARY_KEY,
    CALLBOX_STORAGE_SNTP_FALLBACK_KEY,
};

static const char *const s_u8_keys[] = {
    CALLBOX_STORAGE_WIFI_DHCP_KEY,
    CALLBOX_STORAGE_MQTT_TRANSPORT_KEY,
};

static esp_err_t copy_string_if_present(platform_nvs_handle_t *source,
                                        platform_nvs_handle_t *target,
                                        const char *key)
{
    char value[128];
    bool found = false;
    esp_err_t err = platform_nvs_get_string(source, key, value, sizeof(value), &found);
    if (err != ESP_OK || !found) return err;
    return platform_nvs_set_string(target, key, value);
}

static esp_err_t copy_u8_if_present(platform_nvs_handle_t *source,
                                    platform_nvs_handle_t *target,
                                    const char *key)
{
    uint8_t value = 0;
    bool found = false;
    esp_err_t err = platform_nvs_get_u8(source, key, &value, &found);
    if (err != ESP_OK || !found) return err;
    return platform_nvs_set_u8(target, key, value);
}

static esp_err_t copy_u16_if_present(platform_nvs_handle_t *source,
                                     platform_nvs_handle_t *target,
                                     const char *key)
{
    uint16_t value = 0;
    bool found = false;
    esp_err_t err = platform_nvs_get_u16(source, key, &value, &found);
    if (err != ESP_OK || !found) return err;
    return platform_nvs_set_u16(target, key, value);
}

static esp_err_t verify_string_if_present(platform_nvs_handle_t *source,
                                          platform_nvs_handle_t *target,
                                          const char *key)
{
    char source_value[128];
    char target_value[128];
    bool source_found = false;
    bool target_found = false;
    esp_err_t err = platform_nvs_get_string(source, key, source_value,
                                            sizeof(source_value), &source_found);
    if (err != ESP_OK || !source_found) return err;
    err = platform_nvs_get_string(target, key, target_value,
                                  sizeof(target_value), &target_found);
    if (err != ESP_OK) return err;
    return (target_found && strcmp(source_value, target_value) == 0)
               ? ESP_OK
               : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t verify_u8_if_present(platform_nvs_handle_t *source,
                                      platform_nvs_handle_t *target,
                                      const char *key)
{
    uint8_t source_value = 0;
    uint8_t target_value = 0;
    bool source_found = false;
    bool target_found = false;
    esp_err_t err = platform_nvs_get_u8(source, key, &source_value, &source_found);
    if (err != ESP_OK || !source_found) return err;
    err = platform_nvs_get_u8(target, key, &target_value, &target_found);
    if (err != ESP_OK) return err;
    return (target_found && source_value == target_value)
               ? ESP_OK
               : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t verify_u16_if_present(platform_nvs_handle_t *source,
                                       platform_nvs_handle_t *target,
                                       const char *key)
{
    uint16_t source_value = 0;
    uint16_t target_value = 0;
    bool source_found = false;
    bool target_found = false;
    esp_err_t err = platform_nvs_get_u16(source, key, &source_value, &source_found);
    if (err != ESP_OK || !source_found) return err;
    err = platform_nvs_get_u16(target, key, &target_value, &target_found);
    if (err != ESP_OK) return err;
    return (target_found && source_value == target_value)
               ? ESP_OK
               : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t migrate_wifi_profiles(platform_nvs_handle_t *legacy,
                                       platform_nvs_handle_t *target,
                                       uint8_t *out_count,
                                       bool *out_found)
{
    uint8_t count = 0;
    bool found = false;
    esp_err_t err = platform_nvs_get_u8(legacy, CALLBOX_STORAGE_WIFI_COUNT_KEY,
                                        &count, &found);
    if (err != ESP_OK) return err;

    if (!found) {
        *out_count = 0;
        *out_found = false;
        return ESP_OK;
    }

    if (count > MAX_WIFI_PROFILES) count = MAX_WIFI_PROFILES;
    err = platform_nvs_set_u8(target, CALLBOX_STORAGE_WIFI_COUNT_KEY, count);
    for (uint8_t i = 0; err == ESP_OK && i < count; ++i) {
        char key[16];
        (void)snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                       (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_SSID);
        err = copy_string_if_present(legacy, target, key);
        if (err != ESP_OK) break;
        (void)snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                       (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_PASS);
        err = copy_string_if_present(legacy, target, key);
    }

    *out_count = count;
    *out_found = true;
    return err;
}

static esp_err_t verify_wifi_profiles(platform_nvs_handle_t *legacy,
                                      platform_nvs_handle_t *target,
                                      uint8_t expected_count,
                                      bool count_found)
{
    if (!count_found) return ESP_OK;

    uint8_t copied_count = 0;
    bool copied_count_found = false;
    esp_err_t err = platform_nvs_get_u8(target, CALLBOX_STORAGE_WIFI_COUNT_KEY,
                                        &copied_count, &copied_count_found);
    if (err != ESP_OK) return err;
    if (!copied_count_found || copied_count != expected_count) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    for (uint8_t i = 0; err == ESP_OK && i < expected_count; ++i) {
        char key[16];
        (void)snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                       (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_SSID);
        err = verify_string_if_present(legacy, target, key);
        if (err != ESP_OK) break;
        (void)snprintf(key, sizeof(key), CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT,
                       (unsigned)i, CALLBOX_STORAGE_WIFI_PROFILE_FIELD_PASS);
        err = verify_string_if_present(legacy, target, key);
    }
    return err;
}

static esp_err_t migrate_config(void)
{
    platform_nvs_handle_t legacy = {0};
    esp_err_t err = platform_nvs_open(&legacy, CALLBOX_STORAGE_NAMESPACE, true);
    if (err == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    platform_nvs_handle_t target = {0};
    err = platform_nvs_open_partition(&target, CALLBOX_STORAGE_CFG_PARTITION,
                                      CALLBOX_STORAGE_NAMESPACE, false);
    if (err != ESP_OK) {
        platform_nvs_close(&legacy);
        return err;
    }

    for (size_t i = 0;
         err == ESP_OK && i < sizeof(s_string_keys) / sizeof(s_string_keys[0]);
         ++i) {
        err = copy_string_if_present(&legacy, &target, s_string_keys[i]);
    }
    for (size_t i = 0;
         err == ESP_OK && i < sizeof(s_u8_keys) / sizeof(s_u8_keys[0]);
         ++i) {
        err = copy_u8_if_present(&legacy, &target, s_u8_keys[i]);
    }
    if (err == ESP_OK) {
        err = copy_u16_if_present(&legacy, &target, CALLBOX_STORAGE_MQTT_PORT_KEY);
    }

    uint8_t profile_count = 0;
    bool profile_count_found = false;
    if (err == ESP_OK) {
        err = migrate_wifi_profiles(&legacy, &target,
                                    &profile_count, &profile_count_found);
    }
    if (err == ESP_OK) err = platform_nvs_commit(&target);
    platform_nvs_close(&target);

    if (err == ESP_OK) {
        err = platform_nvs_open_partition(&target, CALLBOX_STORAGE_CFG_PARTITION,
                                          CALLBOX_STORAGE_NAMESPACE, true);
    }
    for (size_t i = 0;
         err == ESP_OK && i < sizeof(s_string_keys) / sizeof(s_string_keys[0]);
         ++i) {
        err = verify_string_if_present(&legacy, &target, s_string_keys[i]);
    }
    for (size_t i = 0;
         err == ESP_OK && i < sizeof(s_u8_keys) / sizeof(s_u8_keys[0]);
         ++i) {
        err = verify_u8_if_present(&legacy, &target, s_u8_keys[i]);
    }
    if (err == ESP_OK) {
        err = verify_u16_if_present(&legacy, &target, CALLBOX_STORAGE_MQTT_PORT_KEY);
    }
    if (err == ESP_OK) {
        err = verify_wifi_profiles(&legacy, &target,
                                   profile_count, profile_count_found);
    }

    if (target.handle) platform_nvs_close(&target);
    platform_nvs_close(&legacy);
    return err;
}

static esp_err_t migrate_sequence(void)
{
    platform_nvs_handle_t legacy = {0};
    esp_err_t err = platform_nvs_open(&legacy, CALLBOX_STORAGE_NAMESPACE, true);
    if (err == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    uint32_t legacy_high_watermark = 0;
    bool legacy_found = false;
    err = platform_nvs_get_u32(&legacy, CALLBOX_STORAGE_SEQ_KEY,
                               &legacy_high_watermark, &legacy_found);
    platform_nvs_close(&legacy);
    if (err != ESP_OK || !legacy_found) return err;

    platform_nvs_handle_t target = {0};
    err = platform_nvs_open_partition(&target, CALLBOX_STORAGE_RUNTIME_PARTITION,
                                      SEQUENCE_NAMESPACE, false);
    if (err != ESP_OK) return err;

    uint32_t current_high_watermark = 0;
    bool current_found = false;
    err = platform_nvs_get_u32(&target, SEQUENCE_HIGH_WATERMARK_KEY,
                               &current_high_watermark, &current_found);
    if (err != ESP_OK) {
        platform_nvs_close(&target);
        return err;
    }

    const uint32_t migrated_high_watermark =
        (current_found && current_high_watermark > legacy_high_watermark)
            ? current_high_watermark
            : legacy_high_watermark;

    err = platform_nvs_set_u32(&target, SEQUENCE_HIGH_WATERMARK_KEY,
                               migrated_high_watermark);
    if (err == ESP_OK) err = platform_nvs_commit(&target);
    platform_nvs_close(&target);

    if (err == ESP_OK) {
        err = platform_nvs_open_partition(&target, CALLBOX_STORAGE_RUNTIME_PARTITION,
                                          SEQUENCE_NAMESPACE, true);
    }
    uint32_t copied_high_watermark = 0;
    bool copied_found = false;
    if (err == ESP_OK) {
        err = platform_nvs_get_u32(&target, SEQUENCE_HIGH_WATERMARK_KEY,
                                   &copied_high_watermark, &copied_found);
    }
    if (target.handle) platform_nvs_close(&target);
    if (err == ESP_OK &&
        (!copied_found || copied_high_watermark != migrated_high_watermark)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

static esp_err_t write_verified_marker(void)
{
    platform_nvs_handle_t marker = {0};
    esp_err_t err = platform_nvs_open_partition(&marker, CALLBOX_STORAGE_CFG_PARTITION,
                                                MIGRATION_NAMESPACE, false);
    if (err != ESP_OK) return err;

    err = platform_nvs_set_u8(&marker, MIGRATION_VERSION_KEY,
                              CALLBOX_STORAGE_SCHEMA_VERSION);
    if (err == ESP_OK) {
        err = platform_nvs_set_u8(&marker, MIGRATION_STATE_KEY, MIGRATION_VERIFIED);
    }
    if (err == ESP_OK) err = platform_nvs_commit(&marker);
    platform_nvs_close(&marker);

    if (err == ESP_OK) {
        err = platform_nvs_open_partition(&marker, CALLBOX_STORAGE_CFG_PARTITION,
                                          MIGRATION_NAMESPACE, true);
    }
    uint8_t state = 0;
    uint8_t version = 0;
    bool state_found = false;
    bool version_found = false;
    if (err == ESP_OK) {
        err = platform_nvs_get_u8(&marker, MIGRATION_VERSION_KEY,
                                  &version, &version_found);
    }
    if (err == ESP_OK) {
        err = platform_nvs_get_u8(&marker, MIGRATION_STATE_KEY,
                                  &state, &state_found);
    }
    if (marker.handle) platform_nvs_close(&marker);
    if (err == ESP_OK &&
        (!state_found || !version_found || state != MIGRATION_VERIFIED ||
         version != CALLBOX_STORAGE_SCHEMA_VERSION)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t callbox_storage_migrate(void)
{
    esp_err_t err = platform_nvs_init_partition(CALLBOX_STORAGE_CFG_PARTITION);
    if (err != ESP_OK) return err;
    err = platform_nvs_init_partition(CALLBOX_STORAGE_RUNTIME_PARTITION);
    if (err != ESP_OK) return err;

    platform_nvs_handle_t marker = {0};
    err = platform_nvs_open_partition(&marker, CALLBOX_STORAGE_CFG_PARTITION,
                                      MIGRATION_NAMESPACE, false);
    if (err != ESP_OK) return err;

    uint8_t state = 0;
    uint8_t version = 0;
    bool state_found = false;
    bool version_found = false;
    err = platform_nvs_get_u8(&marker, MIGRATION_STATE_KEY,
                              &state, &state_found);
    if (err == ESP_OK) {
        err = platform_nvs_get_u8(&marker, MIGRATION_VERSION_KEY,
                                  &version, &version_found);
    }
    if (err == ESP_OK && version_found && version > CALLBOX_STORAGE_SCHEMA_VERSION) {
        platform_nvs_close(&marker);
        return ESP_ERR_INVALID_VERSION;
    }
    if (err == ESP_OK && state_found && version_found &&
        state == MIGRATION_VERIFIED && version == CALLBOX_STORAGE_SCHEMA_VERSION) {
        platform_nvs_close(&marker);
        return ESP_OK;
    }

    if (err == ESP_OK) {
        err = platform_nvs_set_u8(&marker, MIGRATION_STATE_KEY, MIGRATION_IN_PROGRESS);
    }
    if (err == ESP_OK) {
        err = platform_nvs_set_u8(&marker, MIGRATION_VERSION_KEY,
                                  CALLBOX_STORAGE_SCHEMA_VERSION);
    }
    if (err == ESP_OK) err = platform_nvs_commit(&marker);
    platform_nvs_close(&marker);
    if (err != ESP_OK) return err;

    err = migrate_config();
    if (err == ESP_OK) err = migrate_sequence();
    if (err == ESP_OK) err = write_verified_marker();
    return err;
}
