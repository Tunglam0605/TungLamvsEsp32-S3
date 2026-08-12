#include "gateway_topic.h"

#include <stdio.h>
#include <string.h>

#define GATEWAY_WAREHOUSE_NAME_MAX 63U

bool gateway_topic_segment_valid(const char *segment)
{
    if (segment == NULL) return false;
    const size_t length = strnlen(segment, GATEWAY_IDENTITY_SEGMENT_MAX + 1U);
    if (length == 0U || length > GATEWAY_IDENTITY_SEGMENT_MAX) return false;
    if (segment[0] < 'a' || segment[0] > 'z') {
        if (segment[0] < '0' || segment[0] > '9') return false;
    }
    for (size_t i = 1U; i < length; ++i) {
        const char c = segment[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_') {
            continue;
        }
        return false;
    }
    return true;
}

bool gateway_warehouse_name_valid(const char *name)
{
    if (name == NULL) return false;
    const size_t length = strnlen(name, GATEWAY_WAREHOUSE_NAME_MAX + 1U);
    if (length == 0U || length > GATEWAY_WAREHOUSE_NAME_MAX) return false;
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || c == 0x7FU) return false;
    }
    return true;
}

bool gateway_identity_valid(const gateway_config_t *config)
{
    return config != NULL &&
           gateway_topic_segment_valid(config->company_id) &&
           gateway_topic_segment_valid(config->site_id) &&
           gateway_topic_segment_valid(config->warehouse_id) &&
           gateway_warehouse_name_valid(config->warehouse_name);
}

static esp_err_t build_topic(char *destination, size_t capacity,
                             const gateway_config_t *config,
                             const char *suffix)
{
    if (destination == NULL || capacity == 0U || config == NULL || suffix == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const int written = snprintf(destination, capacity,
                                 "warehouse/sensor/%s/%s/%s/%s",
                                 config->company_id, config->site_id,
                                 config->warehouse_id, suffix);
    if (written < 0 || (size_t)written >= capacity) {
        destination[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t gateway_topic_build_set(const gateway_config_t *config,
                                  gateway_topic_set_t *topics)
{
    if (config == NULL || topics == NULL || !gateway_identity_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    gateway_topic_set_t built = {0};
    esp_err_t error = build_topic(built.status_json, sizeof(built.status_json),
                                  config, "status/json");
    if (error == ESP_OK) {
        error = build_topic(built.status_bits, sizeof(built.status_bits),
                            config, "status/bits");
    }
    if (error != ESP_OK) {
        memset(topics, 0, sizeof(*topics));
        return error;
    }
    *topics = built;
    return ESP_OK;
}

esp_err_t gateway_topic_build_legacy_availability(const gateway_config_t *config,
                                                  char *topic,
                                                  size_t capacity)
{
    if (config == NULL || topic == NULL || capacity == 0U ||
        !gateway_identity_valid(config)) {
        if (topic != NULL && capacity > 0U) topic[0] = '\0';
        return ESP_ERR_INVALID_ARG;
    }
    return build_topic(topic, capacity, config, "availability");
}
