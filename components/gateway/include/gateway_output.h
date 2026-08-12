#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "gateway_status.h"

typedef struct {
    bool buzzer;
    bool tower_red;
    bool tower_yellow;
    bool tower_green;
    bool production_network;
    bool mqtt_connected;
} gateway_output_snapshot_t;

esp_err_t gateway_output_start(void);
void gateway_output_set_health(bool production_network, bool mqtt_connected);
esp_err_t gateway_output_report(gateway_diagnostic_event_t event);
void gateway_output_snapshot(gateway_output_snapshot_t *snapshot);
