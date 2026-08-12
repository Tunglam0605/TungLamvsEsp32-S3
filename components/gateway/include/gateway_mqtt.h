#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t gateway_mqtt_start(void);
void gateway_mqtt_reconfigure(void);
bool gateway_mqtt_is_connected(void);
const char *gateway_mqtt_state_topic(void);

