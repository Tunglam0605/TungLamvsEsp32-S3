#include "gateway_network.h"

#include <string.h>
#include "bsp_eth.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_wifi.h"

static const char *TAG = "GW_NET";
static gateway_config_t s_config;
static bool s_started;
static char s_active[33];

static void connect_best(void)
{
    if (!s_config.wifi_profile_count) return;
    platform_wifi_scan_record_t aps[24];
    uint16_t count = 24;
    platform_wifi_scan_config_t scan = {.show_hidden=false,.active_min_ms=80,.active_max_ms=160};
    int best_profile = -1, best_rssi = -128;
    if (platform_wifi_scan(&scan, aps, &count) == ESP_OK) {
        for (uint16_t a = 0; a < count; ++a) for (uint8_t p = 0; p < s_config.wifi_profile_count; ++p) {
            if (!strcmp(aps[a].ssid, s_config.wifi_profiles[p].ssid) && aps[a].rssi > best_rssi) {
                best_profile = p; best_rssi = aps[a].rssi;
            }
        }
    }
    /* Giong CallBox: chi ket noi profile dang thuc su nhin thay. Viec co gang
     * ket noi mot SSID khong ton tai lam Wi-Fi lien tuc disconnect va co the
     * pha vo socket HTTP dang mo tren Ethernet o mot so ban ESP-IDF/W5500. */
    if (best_profile < 0) {
        ESP_LOGD(TAG, "Khong tim thay profile Wi-Fi da luu trong lan quet nay");
        return;
    }
    gateway_wifi_profile_t *p = &s_config.wifi_profiles[best_profile];
    platform_wifi_sta_network_config_t net = {
        .dhcp=s_config.wifi_dhcp,.ip=s_config.wifi_ip,.netmask=s_config.wifi_netmask,
        .gateway=s_config.wifi_gateway,.dns=s_config.wifi_dns};
    (void)platform_wifi_apply_sta_network_config(&net);
    (void)platform_wifi_sta_set_credentials(p->ssid, p->password);
    esp_err_t err = platform_wifi_sta_connect();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        snprintf(s_active, sizeof(s_active), "%s", p->ssid);
        ESP_LOGI(TAG, "STA dang ket noi profile tot nhat: %s (%d dBm)", p->ssid, best_rssi);
    }
}

static void network_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    for (;;) {
        if (!platform_wifi_sta_is_connected()) connect_best();
        vTaskDelay(pdMS_TO_TICKS(platform_wifi_sta_is_connected() ? 30000 : 10000));
    }
}

esp_err_t gateway_network_start(const gateway_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    char ap_ssid[33]; snprintf(ap_ssid, sizeof(ap_ssid), "AUBOT-%s", config->gateway_id);
    platform_wifi_sta_network_config_t sta = {.dhcp=config->wifi_dhcp,.ip=config->wifi_ip,
        .netmask=config->wifi_netmask,.gateway=config->wifi_gateway,.dns=config->wifi_dns};
    platform_wifi_ap_config_t ap = {.ssid=ap_ssid,.password=config->ap_password,
        .ip="192.168.65.204",.netmask="255.255.255.0",.channel=1,.max_clients=4};
    esp_err_t e = platform_wifi_start_apsta(&sta, &ap, NULL, NULL);
    if (e != ESP_OK) return e;
    s_started = true;
    if (xTaskCreate(network_task, "gw_network", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t gateway_network_apply(const gateway_config_t *config)
{
    if (!config || !s_started) return ESP_ERR_INVALID_STATE;
    s_config = *config; s_active[0] = 0;
    (void)platform_wifi_sta_disconnect();
    connect_best();
    const bsp_eth_network_config_t eth = config->eth_router_mode
        ? (bsp_eth_network_config_t) {
            .dhcp=config->eth_dhcp,.ip=config->eth_ip,.netmask=config->eth_netmask,
            .gateway=config->eth_gateway,.dns=config->eth_dns}
        : (bsp_eth_network_config_t) {
            .dhcp=false,.ip="169.254.1.1",.netmask="255.255.0.0",
            .gateway="0.0.0.0",.dns=NULL};
    return bsp_eth_apply_network_config(&eth);
}

bool gateway_network_is_connected(void) { return platform_wifi_sta_is_connected() || bsp_eth_is_connected(); }
const char *gateway_network_active_name(void) { return s_active; }
