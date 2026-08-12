#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t gateway_mqtt_start(void);
void gateway_mqtt_reconfigure(void);
void gateway_mqtt_request_snapshot(void);
bool gateway_mqtt_is_connected(void);
/* Compatibility accessor: the primary state topic is the JSON status topic. */
const char *gateway_mqtt_state_topic(void);
const char *gateway_mqtt_bits_topic(void);
