/**
 * @file queues.h
 * @brief Chỉ chứa hàng đợi (queue) và cấu hình Callbox đã lưu bền vững.
 *
 * Các kiểu miền thuộc về mission_types.h, protocol_types.h và app_event.h.
 */
#ifndef CALLBOX_QUEUES_H
#define CALLBOX_QUEUES_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define CALLBOX_DEVICE_NAME_PREFIX "AUBOT-Callbox-"
#define MAX_WIFI_PROFILES 5

typedef struct {
    char ssid[33];
    char password[64];
} WifiProfile_t;

typedef enum {
    MQTT_TRANSPORT_TCP = 0,
    MQTT_TRANSPORT_TLS = 1,
} MqttTransport_t;

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[64];
    WifiProfile_t wifi_profiles[MAX_WIFI_PROFILES];
    uint8_t wifi_profile_count;
    bool wifi_dhcp;
    char wifi_ip[16];
    char wifi_netmask[16];
    char wifi_gateway[16];
    char wifi_dns[16];
    /* NTP nhà máy chính và một fallback tùy chọn. Chỉ hostname hoặc IPv4. */
    char sntp_primary[64];
    char sntp_fallback[64];
    char mqtt_broker[64];
    uint16_t mqtt_port;
    MqttTransport_t mqtt_transport;
    char mqtt_user[32];
    char mqtt_pass[64];
    char callbox_id[16];
    char web_password[64];
} Config_t;

/* Tải trọng lệnh buzzer được định nghĩa bởi led_control.h. */
extern QueueHandle_t buzzer_queue;
extern Config_t g_config;

#endif /* CALLBOX_QUEUES_H */
