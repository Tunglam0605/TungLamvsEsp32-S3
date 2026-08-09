/**
 * @file    callbox_config.h
 * @brief   Hợp đồng cấu hình sản phẩm CallBox (PURE DATA CONTRACT).
 *
 *          Module này sở hữu duy nhất các kiểu cấu hình product:
 *          Config_t, WifiProfile_t, MqttTransport_t, MAX_WIFI_PROFILES,
 *          CALLBOX_DEVICE_NAME_PREFIX.
 *
 *          ═══ THUẦN (PURE) ═══
 *          Chỉ include generic C headers (stdbool/stdint). Không FreeRTOS,
 *          không QueueHandle_t, không esp_err.h, không BSP/Platform, không
 *          MQTT/Wi-Fi driver, không ESP-NETIF.
 *
 *          ═══ BẤT BIẾN ═══
 *          Layout Config_t là hợp đồng NVS + runtime — tuyệt đối không
 *          rename/reorder/resize/remove/add field.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#ifndef CALLBOX_CONFIG_H
#define CALLBOX_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

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

#endif /* CALLBOX_CONFIG_H */
