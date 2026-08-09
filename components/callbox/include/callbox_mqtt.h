/**
 * @file    callbox_mqtt.h
 * @brief   Giao tiếp MQTT với WCS (Warehouse Control System).
 *
 *          Dùng thư viện ESP-MQTT chuẩn của ESP-IDF (không còn tự viết
 *          socket MQTT): ESP-MQTT đảm nhận framing, keep-alive (PINGREQ),
 *          auto-reconnect và TLS (CA bundle) — giao thức tầng ứng dụng
 *          vẫn là callbox/{id}/{event,cmd,status}.
 *
 *          ═══ TRANSPORT ═══
 *          ┌──────────────────────────┬──────────────────────────────┐
 *          │ MQTT_TRANSPORT_TCP       │ mqtt:// — broker nội bộ      │
 *          │ MQTT_TRANSPORT_TLS       │ mqtts:// — Cloud / Internet, │
 *          │                          │  chứng thực CA bundle, cần   │
 *          │                          │  SNTP đồng bộ giờ trước TLS  │
 *          └──────────────────────────┴──────────────────────────────┘
 *
 *          ═══ TOPIC ═══
 *          ┌──────────────────────────┬──────────────────────────────┐
 *          │ callbox/{id}/event       │ Gửi: call, cancel, sync      │
 *          │ callbox/{id}/cmd         │ Nhận: accepted, assigned, ...│
 *          │ callbox/{id}/status      │ Gửi: status/heartbeat        │
 *          └──────────────────────────┴──────────────────────────────┘
 *
 *          Client ID và topic tự sinh theo ID callbox: AUBOT-Callbox-<id>,
 *          callbox/<id>/...
 *
 * @note    Khi đổi broker/port/mode/user/pass hoặc ID callbox: chỉ
 *          MQTT kết nối lại (mqtt_client_reconfigure) — STA Wi-Fi không
 *          bị reset. Địa điểm thuộc về config NVS (config_portal).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 2.0.0
 * @date    2026
 *
 * @see     mqtt_client.c — triển khai ESP-MQTT
 * @see     state_machine.c — gọi mqtt_publish_call/cancel/sync
 * @see     callbox_sews.c — khởi tạo MQTT
 */
#ifndef CALLBOX_MQTT_H
#define CALLBOX_MQTT_H

#include "freertos/FreeRTOS.h"
#include "callbox_config.h"
#include <time.h>

/* Chủ đề MQTT */
#define MQTT_EVENT_TOPIC    "callbox/%s/event"    // Xuất bản (publish)
#define MQTT_CMD_TOPIC      "callbox/%s/cmd"      // Đăng ký nhận (subscribe)
#define MQTT_STATUS_TOPIC   "callbox/%s/status"   // Xuất bản (publish)
#define CALLBOX_FIRMWARE_VERSION "1.2.0"

/* QoS MQTT */
#define MQTT_QoS 1

/* Khoảng thời gian heartbeat (giây) */
#define HEARTBEAT_INTERVAL_SEC 15

typedef struct {
    char topic[256];
    char payload[512];
    int qos;
    int retain;
} MQTTMsg_t;

/**
 * @brief Khởi tạo client MQTT với cấu hình truyền vào tường minh.
 *
 * Cấu hình được chụp vào snapshot riêng của adapter; không lưu con trỏ
 * vào Config_t của caller (portal có thể sửa đổi khi chạy).
 */
void mqtt_client_init(const Config_t *config);

/**
 * @brief Kết nối tới broker MQTT
 */
void mqtt_client_connect(void);

/**
 * @brief Tạo lại kết nối ESP-MQTT sau khi cấu hình endpoint/bảo mật thay đổi.
 *
 * Nếu runtime MQTT chưa khởi tạo (portal lưu sớm trước mqtt_client_init),
 * chỉ cập nhật snapshot — mqtt_client_init sau đó dùng cấu hình mới nhất.
 */
void mqtt_client_reconfigure(const Config_t *config);

/**
 * @brief Đăng ký nhận topic lệnh
 */
void mqtt_client_subscribe_cmd(void);

/**
 * @brief Publish một sự kiện call theo phạm vi task.
 */
void mqtt_publish_call(int task, uint32_t seq, uint32_t timestamp);

/** @brief Publish một sự kiện cancel theo phạm vi task. */
void mqtt_publish_cancel(int task, uint32_t seq, uint32_t timestamp);

/** @brief Publish yêu cầu đồng bộ WCS theo phạm vi thiết bị. */
void mqtt_publish_sync_request(uint32_t seq, uint32_t timestamp);

/**
 * @brief Publish heartbeat/trạng thái
 */
void mqtt_publish_status(void);

/**
 * @brief MQTT event handler task
 */
void mqtt_event_handler_task(void *pvParameters);

/**
 * @brief MQTT communication task - handles pub/sub
 */
void mqtt_communication_task(void *pvParameters);

/**
 * @brief Check if MQTT is connected
 * @return 1 if connected, 0 if disconnected
 */
uint8_t mqtt_is_connected(void);

#endif // CALLBOX_MQTT_H
