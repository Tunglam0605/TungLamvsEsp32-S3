#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "gateway_config.h"

esp_err_t gateway_network_start(const gateway_config_t *config);
esp_err_t gateway_network_apply(const gateway_config_t *config);
bool gateway_network_is_connected(void);
const char *gateway_network_active_name(void);

