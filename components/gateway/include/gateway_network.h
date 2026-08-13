#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "gateway_config.h"
#include "bsp_eth.h"

/* Product-owned local service networks.  AP and Ethernet intentionally use
 * different /24 networks so both interfaces can remain enabled without an
 * ambiguous route in lwIP. */
#define GATEWAY_AP_IP             "192.168.65.204"
#define GATEWAY_AP_NETMASK        "255.255.255.0"
#define GATEWAY_ETH_DEBUG_IP      BSP_ETH_LOCAL_IP
#define GATEWAY_ETH_DEBUG_NETMASK BSP_ETH_LOCAL_NETMASK
#define GATEWAY_ETH_DEBUG_GATEWAY BSP_ETH_LOCAL_GATEWAY

esp_err_t gateway_network_start(const gateway_config_t *config);
esp_err_t gateway_network_apply(const gateway_config_t *config);
bool gateway_network_is_connected(void);
bool gateway_network_production_available(void);
bool gateway_network_wifi_available(void);
bool gateway_network_eth_uplink_available(void);
bool gateway_network_eth_debug_active(void);
bool gateway_network_production_state(bool wifi_available, bool eth_has_ip,
                                      bool eth_uplink_mode);
esp_err_t gateway_network_set_ap(bool enabled);
bool gateway_network_ap_is_manual(void);
const char *gateway_network_active_name(void);
