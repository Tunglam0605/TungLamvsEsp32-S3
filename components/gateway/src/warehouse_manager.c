#include "warehouse_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "platform_nvs.h"

#define WAREHOUSE_NVS_NAMESPACE "warehouse"

static const char *TAG = "WAREHOUSE";
static warehouse_mapping_t s_mappings[LASER_CAN_MAX_NODES];
static portMUX_TYPE s_mapping_mux = portMUX_INITIALIZER_UNLOCKED;

static void mapping_key(uint8_t laser_id, char key[8])
{
    snprintf(key, 8, "m%02u", laser_id);
}

static bool valid_text(const char *text, size_t capacity, bool allow_empty)
{
    if (text == NULL) return false;
    const size_t len = strnlen(text, capacity);
    if (len >= capacity || (!allow_empty && len == 0U)) return false;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c == '|' || c < 0x20U) return false;
    }
    return true;
}

static bool mapping_valid(const warehouse_mapping_t *mapping)
{
    return mapping != NULL && mapping->laser_id >= 1U &&
           mapping->laser_id <= LASER_CAN_MAX_NODES &&
           mapping->slot_index >= 1U &&
           mapping->slot_index <= LASER_CAN_MAX_NODES &&
           mapping->cluster_id >= 1U &&
           mapping->cluster_id <= WAREHOUSE_MAX_CLUSTERS &&
           valid_text(mapping->slot_code, sizeof(mapping->slot_code), false) &&
           valid_text(mapping->slot_name, sizeof(mapping->slot_name), true);
}

static esp_err_t persist_mapping(const warehouse_mapping_t *mapping, bool clear)
{
    platform_nvs_handle_t handle = { 0 };
    esp_err_t err = platform_nvs_open(&handle, WAREHOUSE_NVS_NAMESPACE, false);
    if (err != ESP_OK) return err;

    char key[8];
    mapping_key(mapping->laser_id, key);
    char value[WAREHOUSE_SLOT_CODE_LEN + WAREHOUSE_SLOT_NAME_LEN + 8U];
    if (clear) {
        value[0] = '\0';
    } else {
        const int len = snprintf(value, sizeof(value), "%u|%u|%s|%s",
                                 mapping->slot_index, mapping->cluster_id, mapping->slot_code,
                                 mapping->slot_name);
        if (len < 0 || (size_t)len >= sizeof(value)) {
            platform_nvs_close(&handle);
            return ESP_ERR_INVALID_SIZE;
        }
    }
    err = platform_nvs_set_string(&handle, key, value);
    if (err == ESP_OK) err = platform_nvs_commit(&handle);
    platform_nvs_close(&handle);
    return err;
}

static bool decode_mapping(uint8_t laser_id, char *value, warehouse_mapping_t *mapping)
{
    char *first = strchr(value, '|');
    if (first == NULL) return false;
    *first++ = '\0';
    char *second = strchr(first, '|');
    if (second == NULL) return false;
    *second++ = '\0';

    /* Schema v2: slot_index|cluster|code|name. Schema cu: cluster|code|name.
     * Du lieu cu duoc migrate an toan voi slot_index = LaserID. */
    char *third = strchr(second, '|');
    uint8_t slot_index = laser_id;
    char *cluster_text = value;
    char *code = first;
    char *name = second;
    if (third != NULL) {
        *third++ = '\0';
        char *end_index = NULL;
        unsigned long parsed_index = strtoul(value, &end_index, 10);
        if (end_index == value || *end_index != '\0' || parsed_index < 1UL ||
            parsed_index > LASER_CAN_MAX_NODES) return false;
        slot_index = (uint8_t)parsed_index;
        cluster_text = first;
        code = second;
        name = third;
    }

    char *end = NULL;
    const unsigned long cluster = strtoul(cluster_text, &end, 10);
    if (end == cluster_text || *end != '\0' || cluster == 0UL ||
        cluster > WAREHOUSE_MAX_CLUSTERS) return false;

    *mapping = (warehouse_mapping_t) {
        .assigned = true,
        .laser_id = laser_id,
        .slot_index = slot_index,
        .cluster_id = (uint8_t)cluster,
    };
    snprintf(mapping->slot_code, sizeof(mapping->slot_code), "%s", code);
    snprintf(mapping->slot_name, sizeof(mapping->slot_name), "%s", name);
    return mapping_valid(mapping);
}

esp_err_t warehouse_manager_init(void)
{
    memset(s_mappings, 0, sizeof(s_mappings));
    platform_nvs_handle_t handle = { 0 };
    esp_err_t err = platform_nvs_open(&handle, WAREHOUSE_NVS_NAMESPACE, true);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No warehouse mapping stored yet");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    for (uint8_t laser_id = 1U; laser_id <= LASER_CAN_MAX_NODES; ++laser_id) {
        char key[8];
        char value[WAREHOUSE_SLOT_CODE_LEN + WAREHOUSE_SLOT_NAME_LEN + 8U] = { 0 };
        bool found = false;
        mapping_key(laser_id, key);
        err = platform_nvs_get_string(&handle, key, value, sizeof(value), &found);
        if (err != ESP_OK) break;
        if (!found || value[0] == '\0') continue;
        warehouse_mapping_t mapping = { 0 };
        if (decode_mapping(laser_id, value, &mapping)) {
            s_mappings[laser_id - 1U] = mapping;
        } else {
            ESP_LOGW(TAG, "Ignore invalid mapping key %s", key);
        }
    }
    platform_nvs_close(&handle);
    return err;
}

esp_err_t warehouse_manager_set_mapping(const warehouse_mapping_t *mapping)
{
    if (!mapping_valid(mapping)) return ESP_ERR_INVALID_ARG;
    for (uint8_t id = 1U; id <= LASER_CAN_MAX_NODES; ++id) {
        warehouse_mapping_t existing = {0};
        if (id != mapping->laser_id && warehouse_manager_get_mapping(id, &existing) &&
            existing.slot_index == mapping->slot_index) return ESP_ERR_INVALID_STATE;
    }
    warehouse_mapping_t copy = *mapping;
    copy.assigned = true;
    esp_err_t err = persist_mapping(&copy, false);
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_mapping_mux);
    s_mappings[copy.laser_id - 1U] = copy;
    taskEXIT_CRITICAL(&s_mapping_mux);
    ESP_LOGI(TAG, "Mapped LaserID %u to cluster %u slot %s",
             copy.laser_id, copy.cluster_id, copy.slot_code);
    return ESP_OK;
}

esp_err_t warehouse_manager_clear_mapping(uint8_t laser_id)
{
    if (laser_id == 0U || laser_id > LASER_CAN_MAX_NODES) {
        return ESP_ERR_INVALID_ARG;
    }
    warehouse_mapping_t mapping = { .laser_id = laser_id };
    esp_err_t err = persist_mapping(&mapping, true);
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_mapping_mux);
    memset(&s_mappings[laser_id - 1U], 0, sizeof(s_mappings[0]));
    taskEXIT_CRITICAL(&s_mapping_mux);
    return ESP_OK;
}

bool warehouse_manager_get_mapping(uint8_t laser_id, warehouse_mapping_t *mapping)
{
    if (mapping == NULL || laser_id == 0U || laser_id > LASER_CAN_MAX_NODES) {
        return false;
    }
    taskENTER_CRITICAL(&s_mapping_mux);
    *mapping = s_mappings[laser_id - 1U];
    taskEXIT_CRITICAL(&s_mapping_mux);
    return mapping->assigned;
}

static warehouse_slot_state_t slot_state(const laser_can_node_status_t *node,
                                         bool detected)
{
    if (!detected || !node->alive) return WAREHOUSE_SLOT_OFFLINE;
    if (!node->status_valid || !node->obstacle_valid) return WAREHOUSE_SLOT_WAITING;
    /* Nghiệp vụ kho chỉ quan tâm có hàng hay không. Hai mức khoảng cách
     * NORMAL/EMERGENCY vẫn được giữ ở trang debug kỹ thuật, nhưng đều là
     * OCCUPIED đối với một vị trí kho. */
    if (node->obstacle_state == LASER_OBSTACLE_NORMAL ||
        node->obstacle_state == LASER_OBSTACLE_EMERGENCY) {
        return WAREHOUSE_SLOT_OCCUPIED;
    }
    return WAREHOUSE_SLOT_EMPTY;
}

size_t warehouse_manager_get_slots(warehouse_slot_status_t *slots, size_t capacity,
                                   warehouse_summary_t *summary)
{
    if (summary != NULL) memset(summary, 0, sizeof(*summary));
    if (slots == NULL || capacity == 0U) return 0U;
    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    size_t count = 0U;
    for (uint8_t laser_id = 1U; laser_id <= LASER_CAN_MAX_NODES && count < capacity;
         ++laser_id) {
        warehouse_mapping_t mapping = { 0 };
        if (!warehouse_manager_get_mapping(laser_id, &mapping)) continue;
        laser_can_node_status_t node = { 0 };
        const bool detected = laser_can_bringup_get_node(laser_id, &node);
        warehouse_slot_status_t *slot = &slots[count++];
        *slot = (warehouse_slot_status_t) {
            .mapping = mapping,
            .sensor_detected = detected,
            .sensor_online = detected && node.alive,
            .b300_group = detected ? (uint8_t)(node.group + 1U) : 0U,
            .state = slot_state(&node, detected),
            .last_seen_ms = detected && now_ms >= node.last_seen_ms
                                ? now_ms - node.last_seen_ms : -1LL,
            .proximity_enabled = detected && node.proximity_enabled,
            .distance_mm = detected ? node.distance_mm : 0U,
            .distance_emergency_mm = detected ? node.distance_emergency_mm : 0U,
        };
        if (summary == NULL) continue;
        ++summary->assigned;
        if (slot->sensor_online) ++summary->online; else ++summary->offline;
        if (slot->state == WAREHOUSE_SLOT_EMPTY) ++summary->empty;
        if (slot->state == WAREHOUSE_SLOT_OCCUPIED) ++summary->occupied;
        if (slot->state == WAREHOUSE_SLOT_CRITICAL) ++summary->critical;
    }
    return count;
}

const char *warehouse_slot_state_name(warehouse_slot_state_t state)
{
    switch (state) {
    case WAREHOUSE_SLOT_WAITING: return "WAITING";
    case WAREHOUSE_SLOT_EMPTY: return "EMPTY";
    case WAREHOUSE_SLOT_OCCUPIED: return "OCCUPIED";
    case WAREHOUSE_SLOT_CRITICAL: return "CRITICAL";
    default: return "OFFLINE";
    }
}
