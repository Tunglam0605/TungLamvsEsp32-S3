/**
 * @file    laser_can_bringup.h
 * @brief   Chế độ bring-up CAN không phá hủy cho sensor laser.
 *
 * Module chỉ phục vụ xác minh bus và phát hiện LaserID thực tế trước khi
 * triển khai Laser Protocol Manager đầy đủ. Nó không gửi proximity control,
 * không trả configuration frame và không thay đổi vùng/quãng đường của sensor.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "laser_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi động task quan sát CAN và heartbeat discovery mỗi 2 giây.
 *
 * Heartbeat là Standard CAN ID 0x001, DLC 0. Sensor đang online được kỳ vọng
 * trả status trên ID 20 + LaserID. Hàm idempotent để gateway_app_run không
 * vô tình tạo hai task cùng đọc một RX queue.
 */
esp_err_t laser_can_bringup_start(void);

#define LASER_CAN_MAX_NODES 64U
#define LASER_CAN_GROUP_COUNT LASER_PROFILE_MAX_GROUPS

typedef enum {
    LASER_OBSTACLE_CLEAR = 0,
    LASER_OBSTACLE_NORMAL,
    LASER_OBSTACLE_EMERGENCY,
} laser_obstacle_state_t;

typedef enum {
    LASER_CONFIG_NONE = 0,
    LASER_CONFIG_PENDING,
    LASER_CONFIG_SENT,
    LASER_CONFIG_VERIFIED,
    LASER_CONFIG_MISMATCH,
    LASER_CONFIG_FAILED,
} laser_config_state_t;

typedef struct {
    bool detected;
    bool alive;
    bool status_valid;
    uint8_t laser_id;
    uint8_t group;
    uint32_t rx_frame_count;
    int64_t last_seen_ms;
    bool proximity_enabled;
    uint16_t distance_mm;
    uint16_t distance_emergency_mm;
    uint8_t low_col;
    uint8_t high_row;
    bool obstacle_valid;
    laser_obstacle_state_t obstacle_state;
    bool config_managed;
    laser_config_state_t config_state;
    uint32_t config_tx_count;
} laser_can_node_status_t;

typedef struct {
    uint8_t laser_id;
    uint16_t distance_mm;
    uint16_t distance_emergency_mm;
    uint8_t low_col;
    uint8_t high_row;
    bool proximity_enabled;
} laser_can_config_request_t;

typedef struct {
    uint8_t group;
    laser_obstacle_state_t state;
    int64_t last_event_ms;
    uint32_t normal_event_count;
    uint32_t emergency_event_count;
} laser_can_group_status_t;

typedef struct {
    bool laser_detected;
    uint8_t laser_id;
    uint16_t last_rx_id;
    uint8_t last_rx_dlc;
    bool last_rx_remote;
    uint32_t rx_frame_count;
    int64_t last_seen_ms;
    uint8_t node_count;
} laser_can_bringup_status_t;

/** Snapshot thread-safe cho trang debug Ethernet. */
void laser_can_bringup_get_status(laser_can_bringup_status_t *status);

size_t laser_can_bringup_get_nodes(laser_can_node_status_t *nodes, size_t capacity);
bool laser_can_bringup_get_node(uint8_t laser_id, laser_can_node_status_t *node);
bool laser_can_bringup_get_group(uint8_t group, laser_can_group_status_t *status);

esp_err_t laser_can_bringup_configure(const laser_can_config_request_t *request,
                                      uint8_t *group_out);
esp_err_t laser_can_bringup_set_profile(laser_profile_t profile);
laser_profile_t laser_can_bringup_profile(void);

const char *laser_can_config_state_name(laser_config_state_t state);
const char *laser_can_obstacle_state_name(laser_obstacle_state_t state);

#ifdef __cplusplus
}
#endif
