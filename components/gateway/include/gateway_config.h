#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define GATEWAY_WIFI_PROFILE_MAX 5U

typedef struct {
    char ssid[33];
    char password[64];
} gateway_wifi_profile_t;

typedef enum {
    GATEWAY_MQTT_TCP = 0,
    GATEWAY_MQTT_TLS = 1,
} gateway_mqtt_transport_t;

typedef struct {
    char gateway_id[17];
    char company_id[32];
    char site_id[32];
    char warehouse_id[32];
    char warehouse_name[64];
    char ap_password[64];
    gateway_wifi_profile_t wifi_profiles[GATEWAY_WIFI_PROFILE_MAX];
    uint8_t wifi_profile_count;
    bool wifi_dhcp;
    char wifi_ip[16];
    char wifi_netmask[16];
    char wifi_gateway[16];
    char wifi_dns[16];
    bool eth_router_mode; /* false = debug PC 169.254.1.1 */
    bool eth_dhcp;
    char eth_ip[16];
    char eth_netmask[16];
    char eth_gateway[16];
    char eth_dns[16];
    char mqtt_broker[96];
    uint16_t mqtt_port;
    gateway_mqtt_transport_t mqtt_transport;
    char mqtt_user[48];
    char mqtt_password[64];
    uint16_t publish_interval_ms;
    char sntp_primary[64];
    char sntp_fallback[64];
    char timezone[64];
} gateway_config_t;

esp_err_t gateway_config_init(void);
void gateway_config_get(gateway_config_t *config);
esp_err_t gateway_config_save(const gateway_config_t *config);
void gateway_config_build_ap_identity(const char *gateway_id,
                                      char *ssid, size_t ssid_capacity,
                                      char *password, size_t password_capacity);
bool gateway_config_add_wifi(gateway_config_t *config, const char *ssid,
                             const char *password);
bool gateway_config_remove_wifi(gateway_config_t *config, const char *ssid);
