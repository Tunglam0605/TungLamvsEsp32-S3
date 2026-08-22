#pragma once
#include "esp_err.h"

typedef enum {
    GATEWAY_DIAG_AP_ON = 0,
    GATEWAY_DIAG_AP_OFF,
    GATEWAY_DIAG_NETWORK_UP,
    GATEWAY_DIAG_NETWORK_DOWN,
    GATEWAY_DIAG_MQTT_UP,
    GATEWAY_DIAG_MQTT_DOWN,
    GATEWAY_DIAG_CAN_BUS_OFF,
    GATEWAY_DIAG_CAN_RECOVERED,
    GATEWAY_DIAG_LASER_OFFLINE,
    GATEWAY_DIAG_LASER_RECOVERED,
    GATEWAY_DIAG_CONFIG_MISMATCH,
    GATEWAY_DIAG_CONFIG_APPLIED,
    GATEWAY_DIAG_EVENT_COUNT,
} gateway_diagnostic_event_t;

esp_err_t gateway_status_start(void);
esp_err_t gateway_diagnostic_report(gateway_diagnostic_event_t event);

