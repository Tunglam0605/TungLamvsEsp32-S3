/**
 * @file    laser_can_bringup.c
 * @brief Laser protocol runtime: discovery, status/Warn decoding, group profile,
 * configuration handshake, sensor timeout and CAN health diagnostics.
 */
#include "laser_can_bringup.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bsp_can.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LASER_CAN_HEARTBEAT_ID          0x001U
#define LASER_CAN_GROUP_CONFIG_ID        0x000U
#define LASER_CAN_STATUS_ID_FIRST       20U
#define LASER_CAN_STATUS_ID_LAST        83U
#define LASER_CAN_CONFIG_REQUEST_FIRST  120U
#define LASER_CAN_CONFIG_REQUEST_LAST   183U
#define LASER_CAN_EMERGENCY_ID_FIRST    100U
#define LASER_CAN_NORMAL_ID_FIRST       110U
#define LASER_CAN_HEARTBEAT_PERIOD_MS   500U
#define LASER_CAN_B300_GROUP_COUNT      LASER_PROFILE_MAX_GROUPS
#define LASER_CAN_HEALTH_PERIOD_MS      10000U
#define LASER_CAN_NODE_TIMEOUT_MS       2000U
#define LASER_CAN_OBSTACLE_TIMEOUT_MS   2000U
#define LASER_CAN_DISTANCE_MAX_MM       1200U
#define LASER_CAN_STARTUP_RESTORE_DELAY_MS  1500U
#define LASER_CAN_STARTUP_RESTORE_GAP_MS     400U

typedef struct {
    bool armed;
    uint16_t distance_mm;
    uint16_t distance_emergency_mm;
    uint8_t low_col;
    uint8_t high_row;
    bool proximity_enabled;
} laser_group_config_t;

typedef struct {
    int64_t last_normal_ms;
    int64_t last_emergency_ms;
    uint32_t normal_event_count;
    uint32_t emergency_event_count;
} laser_group_obstacle_t;

static const char *TAG = "LASER_CAN_TEST";
static TaskHandle_t s_bringup_task;
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;
static laser_can_bringup_status_t s_status;
static laser_can_node_status_t s_nodes[LASER_CAN_MAX_NODES];
static laser_group_config_t s_group_config[LASER_CAN_B300_GROUP_COUNT];
static laser_group_obstacle_t s_group_obstacle[LASER_CAN_B300_GROUP_COUNT];
/* The product has one physical layout: 12 B300 groups / 12 warehouse slots. */
static const laser_profile_t s_profile = LASER_PROFILE_GROUP_12;

static uint8_t b300_group_for_laser(uint8_t laser_id)
{
    uint8_t group_id = 0U;
    return laser_profile_group_for_id(s_profile, laser_id, &group_id)
               ? (uint8_t)(group_id - 1U) : UINT8_MAX;
}

static bool valid_config_request(const laser_can_config_request_t *request,
                                 uint8_t *group_out)
{
    if (request == NULL || request->laser_id == 0U ||
        request->laser_id > LASER_CAN_MAX_NODES ||
        request->distance_mm > LASER_CAN_DISTANCE_MAX_MM ||
        request->distance_emergency_mm > request->distance_mm) {
        return false;
    }
    const uint8_t group = b300_group_for_laser(request->laser_id);
    if (group == UINT8_MAX || group >= LASER_CAN_B300_GROUP_COUNT) {
        return false;
    }
    if (group_out != NULL) {
        *group_out = group;
    }
    return true;
}

static laser_group_config_t group_config_from_request(
    const laser_can_config_request_t *request)
{
    return (laser_group_config_t) {
        .armed = true,
        .distance_mm = request->distance_mm,
        .distance_emergency_mm = request->distance_emergency_mm,
        .low_col = request->low_col,
        .high_row = request->high_row,
        .proximity_enabled = request->proximity_enabled,
    };
}

static bool node_matches_config(const laser_can_node_status_t *node,
                                const laser_group_config_t *desired)
{
    return node->status_valid &&
           node->proximity_enabled == desired->proximity_enabled &&
           node->distance_mm == desired->distance_mm &&
           node->distance_emergency_mm == desired->distance_emergency_mm &&
           node->low_col == desired->low_col &&
           node->high_row == desired->high_row;
}

static esp_err_t send_group_proximity(uint8_t group,
                                      const laser_group_config_t *config)
{
    const bsp_can_frame_t proximity = {
        .id = 0x002U,
        .dlc = 2U,
        .data = { group, config->proximity_enabled ? 1U : 0U },
    };
    return bsp_can_send(&proximity, 100U);
}

static bool frame_laser_id(const bsp_can_frame_t *frame, uint8_t *laser_id)
{
    if (frame->id >= LASER_CAN_STATUS_ID_FIRST &&
        frame->id <= LASER_CAN_STATUS_ID_LAST) {
        *laser_id = (uint8_t)(frame->id - LASER_CAN_STATUS_ID_FIRST + 1U);
        return true;
    }
    if (frame->id >= LASER_CAN_CONFIG_REQUEST_FIRST &&
        frame->id <= LASER_CAN_CONFIG_REQUEST_LAST) {
        *laser_id = (uint8_t)(frame->id - LASER_CAN_CONFIG_REQUEST_FIRST + 1U);
        return true;
    }
    return false;
}

static void update_rx_status(const bsp_can_frame_t *frame)
{
    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    uint8_t laser_id = 0;
    const bool is_laser = frame_laser_id(frame, &laser_id);

    taskENTER_CRITICAL(&s_status_mux);
    ++s_status.rx_frame_count;
    s_status.last_rx_id = frame->id;
    s_status.last_rx_dlc = frame->dlc;
    s_status.last_rx_remote = frame->is_remote;
    s_status.last_seen_ms = now_ms;
    if (is_laser) {
        laser_can_node_status_t *node = &s_nodes[laser_id - 1U];
        if (!node->detected) {
            memset(node, 0, sizeof(*node));
            node->detected = true;
            node->laser_id = laser_id;
            node->group = b300_group_for_laser(laser_id);
            ++s_status.node_count;
        }
        node->alive = true;
        node->last_seen_ms = now_ms;
        ++node->rx_frame_count;
        s_status.laser_detected = true;
        s_status.laser_id = laser_id;
    }
    taskEXIT_CRITICAL(&s_status_mux);
}

/* In frame ở task context, không dùng trong ISR. Chuỗi payload có kích thước
 * đủ cho "AA BB CC DD EE FF 11 22" và null terminator. */
static void log_frame(const bsp_can_frame_t *frame)
{
    char bytes[(BSP_CAN_MAX_DLC * 3U) + 1U] = { 0 };
    size_t offset = 0;
    for (uint8_t i = 0; i < frame->dlc && offset + 3U < sizeof(bytes); ++i) {
        const int written = snprintf(&bytes[offset], sizeof(bytes) - offset,
                                     "%02X%s", frame->data[i],
                                     i + 1U == frame->dlc ? "" : " ");
        if (written <= 0) break;
        offset += (size_t)written;
    }
    ESP_LOGI(TAG, "RX id=0x%03X dlc=%u type=%s data=[%s]", frame->id, frame->dlc,
             frame->is_remote ? "REMOTE" : "DATA", bytes);
}

/* Decode tối thiểu duy nhất đã được tài liệu xác nhận cho response status:
 * byte0 enable/status, byte1..2 Distance BE, byte3..4 Distance_E BE,
 * byte5 low_col, byte6 high_row. Không suy diễn bitmap 8x8 từ hai field cuối. */
static void log_sensor_status(const bsp_can_frame_t *frame)
{
    if (frame->dlc < 7U) {
        ESP_LOGW(TAG, "LaserID %u status malformed: DLC=%u, expected >=7",
                 (unsigned)(frame->id - LASER_CAN_STATUS_ID_FIRST + 1U), frame->dlc);
        return;
    }

    const uint8_t laser_id = (uint8_t)(frame->id - LASER_CAN_STATUS_ID_FIRST + 1U);
    const uint16_t distance = ((uint16_t)frame->data[1] << 8U) | frame->data[2];
    const uint16_t distance_emergency = ((uint16_t)frame->data[3] << 8U) |
                                        frame->data[4];
    ESP_LOGI(TAG,
             "LASER ONLINE: LaserID=%u enabled=%u distance=%u mm emergency=%u mm "
             "low_col=%u high_row=%u",
             laser_id, frame->data[0], distance, distance_emergency,
             frame->data[5], frame->data[6]);
}

static void update_sensor_status(const bsp_can_frame_t *frame)
{
    if (frame->is_remote || frame->dlc < 7U) {
        return;
    }

    const uint8_t laser_id = (uint8_t)(frame->id - LASER_CAN_STATUS_ID_FIRST + 1U);
    const uint8_t group = b300_group_for_laser(laser_id);
    const uint16_t distance = ((uint16_t)frame->data[1] << 8U) | frame->data[2];
    const uint16_t distance_emergency = ((uint16_t)frame->data[3] << 8U) |
                                        frame->data[4];

    taskENTER_CRITICAL(&s_status_mux);
    laser_can_node_status_t *node = &s_nodes[laser_id - 1U];
    node->status_valid = true;
    node->proximity_enabled = frame->data[0] != 0U;
    node->distance_mm = distance;
    node->distance_emergency_mm = distance_emergency;
    node->low_col = frame->data[5];
    node->high_row = frame->data[6];
    if (frame->dlc >= 8U) {
        node->obstacle_valid = frame->data[7] <= 2U;
        node->obstacle_state = frame->data[7] == 1U
                                   ? LASER_OBSTACLE_EMERGENCY
                                   : frame->data[7] == 2U
                                         ? LASER_OBSTACLE_NORMAL
                                         : LASER_OBSTACLE_CLEAR;
    }
    if (group < LASER_CAN_B300_GROUP_COUNT && s_group_config[group].armed) {
        const laser_group_config_t *desired = &s_group_config[group];
        node->config_managed = true;
        node->config_state = node_matches_config(node, desired)
                                 ? LASER_CONFIG_VERIFIED
                                 : LASER_CONFIG_MISMATCH;
    }
    taskEXIT_CRITICAL(&s_status_mux);
}

static void reply_to_config_request(uint8_t laser_id)
{
    const uint8_t group = b300_group_for_laser(laser_id);
    laser_group_config_t config = { 0 };

    if (group == UINT8_MAX || group >= LASER_CAN_B300_GROUP_COUNT) {
        ESP_LOGW(TAG, "Ignore config request from unsupported LaserID=%u", laser_id);
        return;
    }

    taskENTER_CRITICAL(&s_status_mux);
    config = s_group_config[group];
    taskEXIT_CRITICAL(&s_status_mux);
    if (!config.armed) {
        ESP_LOGI(TAG, "LaserID %u requested config; group %u is not managed yet",
                 laser_id, group);
        return;
    }

    const bsp_can_frame_t response = {
        .id = (uint16_t)(LASER_CAN_CONFIG_REQUEST_FIRST + laser_id - 1U),
        .dlc = 8,
        .data = {
            (uint8_t)(config.distance_mm >> 8U),
            (uint8_t)(config.distance_mm & 0xFFU),
            (uint8_t)(config.distance_emergency_mm >> 8U),
            (uint8_t)(config.distance_emergency_mm & 0xFFU),
            0U, config.low_col, 0U, config.high_row,
        },
    };
    const esp_err_t response_error = bsp_can_send(&response, 100U);
    const esp_err_t proximity_error = response_error == ESP_OK
        ? send_group_proximity(group, &config) : ESP_FAIL;
    const bool complete = response_error == ESP_OK && proximity_error == ESP_OK;

    taskENTER_CRITICAL(&s_status_mux);
    laser_can_node_status_t *node = &s_nodes[laser_id - 1U];
    node->config_managed = true;
    node->config_state = complete ? LASER_CONFIG_SENT : LASER_CONFIG_FAILED;
    if (response_error == ESP_OK) {
        ++node->config_tx_count;
    }
    taskEXIT_CRITICAL(&s_status_mux);

    if (complete) {
        ESP_LOGI(TAG, "TX restored config LaserID=%u group=%u distance=%u emergency=%u col=%u row=%u enable=%u",
                 laser_id, group, config.distance_mm, config.distance_emergency_mm,
                 config.low_col, config.high_row, config.proximity_enabled);
    } else {
        ESP_LOGE(TAG, "Config TX LaserID=%u failed: response=%s proximity=%s",
                 laser_id, esp_err_to_name(response_error),
                 esp_err_to_name(proximity_error));
    }
}

static bool handle_obstacle_event(const bsp_can_frame_t *frame)
{
    uint8_t group_id = 0U;
    bool emergency = false;
    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    if (!laser_profile_group_for_obstacle_can_id(s_profile, frame->id,
                                                  &group_id, &emergency)) {
        return false;
    }
    const uint8_t group = (uint8_t)(group_id - 1U);
    if (emergency) {
        taskENTER_CRITICAL(&s_status_mux);
        s_group_obstacle[group].last_emergency_ms = now_ms;
        ++s_group_obstacle[group].emergency_event_count;
        taskEXIT_CRITICAL(&s_status_mux);
        ESP_LOGW(TAG, "OBSTACLE EMERGENCY: B300 group=%u id=0x%03X", group_id, frame->id);
        return true;
    } else {
        taskENTER_CRITICAL(&s_status_mux);
        s_group_obstacle[group].last_normal_ms = now_ms;
        ++s_group_obstacle[group].normal_event_count;
        taskEXIT_CRITICAL(&s_status_mux);
        ESP_LOGI(TAG, "OBSTACLE NORMAL: B300 group=%u id=0x%03X", group_id, frame->id);
        return true;
    }
}

static void handle_rx_frame(const bsp_can_frame_t *frame)
{
    update_rx_status(frame);
    log_frame(frame);

    if (handle_obstacle_event(frame)) {
        return;
    }

    if (frame->id >= LASER_CAN_STATUS_ID_FIRST &&
        frame->id <= LASER_CAN_STATUS_ID_LAST) {
        if (frame->is_remote) {
            ESP_LOGW(TAG, "Ignore unexpected REMOTE status frame id=0x%03X", frame->id);
        } else {
            update_sensor_status(frame);
            log_sensor_status(frame);
        }
    } else if (frame->id >= LASER_CAN_CONFIG_REQUEST_FIRST &&
               frame->id <= LASER_CAN_CONFIG_REQUEST_LAST) {
        ESP_LOGI(TAG,
                 "LASER DISCOVERED: LaserID=%u, config request id=0x%03X type=%s",
                 (unsigned)(frame->id - LASER_CAN_CONFIG_REQUEST_FIRST + 1U), frame->id,
                 frame->is_remote ? "REMOTE" : "DATA");
        reply_to_config_request((uint8_t)(frame->id - LASER_CAN_CONFIG_REQUEST_FIRST + 1U));
    }
}

static void send_discovery_heartbeat(void)
{
    const bsp_can_frame_t heartbeat = {
        .id = LASER_CAN_HEARTBEAT_ID,
        .dlc = 0,
    };
    const esp_err_t err = bsp_can_send(&heartbeat, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Heartbeat TX failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "TX discovery heartbeat: id=0x001 dlc=0");
    }
}

/* A Laser can ask for its profile before the ESP32 CAN RX task is ready on a
 * simultaneous cold boot.  Trigger the existing reload handshake once for
 * every configuration that Warehouse Manager restored and explicitly armed. */
static bool trigger_startup_restore(uint8_t group)
{
    laser_group_config_t config = { 0 };
    taskENTER_CRITICAL(&s_status_mux);
    if (group < LASER_CAN_B300_GROUP_COUNT) {
        config = s_group_config[group];
    }
    taskEXIT_CRITICAL(&s_status_mux);
    if (!config.armed) {
        return false;
    }

    const esp_err_t proximity_error = send_group_proximity(group, &config);
    const bsp_can_frame_t reload = {
        .id = LASER_CAN_GROUP_CONFIG_ID,
        .dlc = 1U,
        .data = { group },
    };
    const esp_err_t reload_error = proximity_error == ESP_OK
        ? bsp_can_send(&reload, 100U) : ESP_FAIL;

    if (proximity_error == ESP_OK && reload_error == ESP_OK) {
        ESP_LOGI(TAG, "TX startup restore trigger group=%u enable=%u",
                 group, config.proximity_enabled);
    } else {
        ESP_LOGE(TAG, "Startup restore trigger group=%u failed: proximity=%s reload=%s",
                 group, esp_err_to_name(proximity_error),
                 esp_err_to_name(reload_error));
    }
    return true;
}

static void log_can_health(void)
{
    bsp_can_status_t status = { 0 };
    bsp_can_get_status(&status);
    ESP_LOGI(TAG, "CAN health: state=%d tx_err=%u rx_err=%u bus_err=%" PRIu32
                  " tx_ok=%" PRIu32 " tx_fail=%" PRIu32
                  " rx_cb=%" PRIu32 " rx_q=%" PRIu32
                  " rx_read_fail=%" PRIu32 " rx_reject=%" PRIu32
                  " rx_drop=%" PRIu32,
             status.state, status.tx_error_count, status.rx_error_count,
             status.bus_error_count, status.tx_success_count, status.tx_failed_count,
             status.rx_callback_count, status.rx_enqueued_count,
             status.rx_read_failed_count, status.rx_rejected_count,
             status.rx_queue_overflow_count);
    if (status.rx_rejected_count > 0U) {
        ESP_LOGW(TAG, "RX rejected detail: ide=%" PRIu32 " rtr=%" PRIu32
                      " fdf=%" PRIu32 " bad_id=%" PRIu32 " bad_dlc=%" PRIu32
                      " last{id=0x%08" PRIX32 ",dlc=%u,flags=0x%02X}",
                 status.rx_rejected_ide_count, status.rx_rejected_rtr_count,
                 status.rx_rejected_fdf_count, status.rx_rejected_id_count,
                 status.rx_rejected_dlc_count, status.last_rejected_id,
                 status.last_rejected_dlc, status.last_rejected_flags);
    }
    ESP_LOGI(TAG, "CAN errors: callbacks=%" PRIu32 " last=0x%02" PRIX32
                  " arb=%" PRIu32 " bit=%" PRIu32 " form=%" PRIu32
                  " stuff=%" PRIu32 " ack=%" PRIu32,
             status.error_callback_count, status.last_error_flags,
             status.arbitration_lost_count, status.bit_error_count,
             status.form_error_count, status.stuff_error_count,
             status.ack_error_count);
    if (status.state == BSP_CAN_STATE_BUS_OFF) {
        const esp_err_t err = bsp_can_recover();
        ESP_LOGW(TAG, "CAN bus-off recovery requested: %s", esp_err_to_name(err));
    }
}

static void laser_can_bringup_task(void *context)
{
    (void)context;
    const int64_t heartbeat_period_us =
        (int64_t)LASER_CAN_HEARTBEAT_PERIOD_MS * 1000LL;
    int64_t next_heartbeat_us = esp_timer_get_time();
    int64_t next_health_us = 0;
    uint8_t startup_restore_group = 0U;
    int64_t next_startup_restore_us = next_heartbeat_us +
        (int64_t)LASER_CAN_STARTUP_RESTORE_DELAY_MS * 1000LL;

    for (;;) {
        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_heartbeat_us) {
            send_discovery_heartbeat();
            next_heartbeat_us += heartbeat_period_us;
            if (now_us >= next_heartbeat_us) {
                next_heartbeat_us = now_us + heartbeat_period_us;
            }
        }
        if (now_us >= next_health_us) {
            log_can_health();
            next_health_us = now_us + (int64_t)LASER_CAN_HEALTH_PERIOD_MS * 1000LL;
        }
        if (startup_restore_group < LASER_CAN_B300_GROUP_COUNT &&
            now_us >= next_startup_restore_us) {
            bool triggered = false;
            while (startup_restore_group < LASER_CAN_B300_GROUP_COUNT &&
                   !triggered) {
                triggered = trigger_startup_restore(startup_restore_group++);
            }
            if (triggered) {
                next_startup_restore_us = now_us +
                    (int64_t)LASER_CAN_STARTUP_RESTORE_GAP_MS * 1000LL;
            }
        }

        bsp_can_frame_t frame = { 0 };
        if (bsp_can_receive(&frame, 100U) == ESP_OK) {
            handle_rx_frame(&frame);
        }
    }
}

esp_err_t laser_can_bringup_start(void)
{
    if (s_bringup_task != NULL) {
        return ESP_OK;
    }
    taskENTER_CRITICAL(&s_status_mux);
    memset(&s_status, 0, sizeof(s_status));
    memset(s_nodes, 0, sizeof(s_nodes));
    /* Warehouse Manager may have restored desired configurations from NVS
     * before this RX task starts. Do not erase that table here. */
    memset(s_group_obstacle, 0, sizeof(s_group_obstacle));
    taskEXIT_CRITICAL(&s_status_mux);

    if (xTaskCreate(laser_can_bringup_task, "laser_can_test", 4096U, NULL,
                    6U, &s_bringup_task) != pdPASS) {
        s_bringup_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Laser protocol runtime started: heartbeat every %u ms",
             LASER_CAN_HEARTBEAT_PERIOD_MS);
    return ESP_OK;
}

void laser_can_bringup_get_status(laser_can_bringup_status_t *status)
{
    if (status == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_status_mux);
    *status = s_status;
    taskEXIT_CRITICAL(&s_status_mux);
}

size_t laser_can_bringup_get_nodes(laser_can_node_status_t *nodes, size_t capacity)
{
    if (nodes == NULL || capacity == 0U) {
        return 0U;
    }

    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    size_t count = 0U;
    taskENTER_CRITICAL(&s_status_mux);
    for (uint8_t index = 0U; index < LASER_CAN_MAX_NODES && count < capacity; ++index) {
        if (!s_nodes[index].detected) {
            continue;
        }
        nodes[count] = s_nodes[index];
        nodes[count].alive = now_ms >= nodes[count].last_seen_ms &&
                             now_ms - nodes[count].last_seen_ms <=
                                 (int64_t)LASER_CAN_NODE_TIMEOUT_MS;
        ++count;
    }
    taskEXIT_CRITICAL(&s_status_mux);
    return count;
}

bool laser_can_bringup_get_node(uint8_t laser_id, laser_can_node_status_t *node)
{
    if (node == NULL || laser_id == 0U || laser_id > LASER_CAN_MAX_NODES) {
        return false;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    taskENTER_CRITICAL(&s_status_mux);
    const bool detected = s_nodes[laser_id - 1U].detected;
    if (detected) {
        *node = s_nodes[laser_id - 1U];
        node->alive = now_ms >= node->last_seen_ms &&
                      now_ms - node->last_seen_ms <=
                          (int64_t)LASER_CAN_NODE_TIMEOUT_MS;
    }
    taskEXIT_CRITICAL(&s_status_mux);
    return detected;
}

bool laser_can_bringup_get_group(uint8_t group, laser_can_group_status_t *status)
{
    if (status == NULL || group >= laser_profile_group_count(s_profile)) {
        return false;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    laser_group_obstacle_t obstacle = { 0 };
    taskENTER_CRITICAL(&s_status_mux);
    obstacle = s_group_obstacle[group];
    taskEXIT_CRITICAL(&s_status_mux);

    const bool emergency_active = obstacle.last_emergency_ms > 0LL &&
        now_ms >= obstacle.last_emergency_ms &&
        now_ms - obstacle.last_emergency_ms <= (int64_t)LASER_CAN_OBSTACLE_TIMEOUT_MS;
    const bool normal_active = obstacle.last_normal_ms > 0LL &&
        now_ms >= obstacle.last_normal_ms &&
        now_ms - obstacle.last_normal_ms <= (int64_t)LASER_CAN_OBSTACLE_TIMEOUT_MS;
    *status = (laser_can_group_status_t) {
        .group = group,
        .state = emergency_active ? LASER_OBSTACLE_EMERGENCY
                                  : normal_active ? LASER_OBSTACLE_NORMAL
                                                  : LASER_OBSTACLE_CLEAR,
        .last_event_ms = obstacle.last_emergency_ms > obstacle.last_normal_ms
                             ? obstacle.last_emergency_ms : obstacle.last_normal_ms,
        .normal_event_count = obstacle.normal_event_count,
        .emergency_event_count = obstacle.emergency_event_count,
    };
    return true;
}

esp_err_t laser_can_bringup_configure(const laser_can_config_request_t *request,
                                      uint8_t *group_out)
{
    uint8_t group = 0U;
    if (!valid_config_request(request, &group)) {
        return ESP_ERR_INVALID_ARG;
    }

    bool detected = false;
    taskENTER_CRITICAL(&s_status_mux);
    detected = s_nodes[request->laser_id - 1U].detected;
    s_group_config[group] = group_config_from_request(request);
    if (detected) {
        for (uint8_t index = 0U; index < LASER_CAN_MAX_NODES; ++index) {
            if (s_nodes[index].detected && s_nodes[index].group == group) {
                s_nodes[index].config_managed = true;
                s_nodes[index].config_state = LASER_CONFIG_PENDING;
            }
        }
    }
    taskEXIT_CRITICAL(&s_status_mux);
    if (!detected) {
        if (group_out != NULL) *group_out = group;
        ESP_LOGI(TAG, "Config armed for offline LaserID=%u group=%u", request->laser_id, group);
        return ESP_OK;
    }
    const laser_group_config_t desired = group_config_from_request(request);
    esp_err_t err = send_group_proximity(group, &desired);
    if (err != ESP_OK) {
        return err;
    }

    /* ID 0 makes every sensor in the selected B300 group reboot and request
     * its DLC8 configuration. Send it only for an explicit API request. */
    const bsp_can_frame_t reload = {
        .id = LASER_CAN_GROUP_CONFIG_ID,
        .dlc = 1U,
        .data = { group },
    };
    err = bsp_can_send(&reload, 100U);
    if (group_out != NULL) {
        *group_out = group;
    }
    ESP_LOGI(TAG, "Config armed by API: LaserID=%u B300 group=%u enable=%u",
             request->laser_id, group, request->proximity_enabled);
    return err;
}

esp_err_t laser_can_bringup_replace_configs(
    const laser_can_config_request_t *requests, size_t request_count)
{
    if ((requests == NULL && request_count != 0U) ||
        request_count > LASER_CAN_B300_GROUP_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    /* This API is called during boot on the small main task stack. */
    static laser_group_config_t replacement[LASER_CAN_B300_GROUP_COUNT];
    memset(replacement, 0, sizeof(replacement));
    for (size_t index = 0U; index < request_count; ++index) {
        uint8_t group = 0U;
        if (!valid_config_request(&requests[index], &group)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (replacement[group].armed) {
            return ESP_ERR_INVALID_STATE;
        }
        replacement[group] = group_config_from_request(&requests[index]);
    }

    taskENTER_CRITICAL(&s_status_mux);
    memcpy(s_group_config, replacement, sizeof(s_group_config));
    for (uint8_t index = 0U; index < LASER_CAN_MAX_NODES; ++index) {
        laser_can_node_status_t *node = &s_nodes[index];
        if (!node->detected || node->group >= LASER_CAN_B300_GROUP_COUNT ||
            !s_group_config[node->group].armed) {
            node->config_managed = false;
            node->config_state = LASER_CONFIG_NONE;
            continue;
        }
        node->config_managed = true;
        node->config_state = node->status_valid
            ? (node_matches_config(node, &s_group_config[node->group])
                   ? LASER_CONFIG_VERIFIED : LASER_CONFIG_MISMATCH)
            : LASER_CONFIG_PENDING;
    }
    taskEXIT_CRITICAL(&s_status_mux);

    ESP_LOGI(TAG, "Restored %u managed Laser group configuration(s)",
             (unsigned)request_count);
    return ESP_OK;
}

const char *laser_can_config_state_name(laser_config_state_t state)
{
    switch (state) {
    case LASER_CONFIG_PENDING: return "PENDING";
    case LASER_CONFIG_SENT: return "SENT_UNVERIFIED";
    case LASER_CONFIG_VERIFIED: return "VERIFIED";
    case LASER_CONFIG_MISMATCH: return "MISMATCH";
    case LASER_CONFIG_FAILED: return "FAILED";
    default: return "UNMANAGED";
    }
}

const char *laser_can_obstacle_state_name(laser_obstacle_state_t state)
{
    switch (state) {
    case LASER_OBSTACLE_NORMAL: return "NORMAL";
    case LASER_OBSTACLE_EMERGENCY: return "EMERGENCY";
    default: return "CLEAR";
    }
}
