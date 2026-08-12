#include "gateway_network.h"

#include <string.h>
#include "bsp_eth.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "platform_wifi.h"

static const char *TAG = "GW_NET";
static gateway_config_t s_config;
static bool s_started;
static char s_active[33];
static bool s_ap_manual;
static int64_t s_ap_started_ms;
#define COMMISSIONING_AP_TIMEOUT_MS 300000LL

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
    int64_t next_sta_attempt_ms = 0;
    for (;;) {
        const int64_t now_ms = esp_timer_get_time() / 1000LL;
        if (!platform_wifi_sta_is_connected() && now_ms >= next_sta_attempt_ms) {
            connect_best();
            next_sta_attempt_ms = now_ms + 10000LL;
        }
        if (!s_ap_manual && platform_wifi_ap_is_active() &&
            now_ms - s_ap_started_ms >= COMMISSIONING_AP_TIMEOUT_MS) {
            (void)platform_wifi_ap_stop();
            ESP_LOGI(TAG, "AP commissioning auto off after 5 minutes");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t gateway_network_start(const gateway_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    char ap_ssid[33], ap_password[64];
    gateway_config_build_ap_identity(config->gateway_id, ap_ssid, sizeof(ap_ssid),
                                     ap_password, sizeof(ap_password));
    platform_wifi_sta_network_config_t sta = {.dhcp=config->wifi_dhcp,.ip=config->wifi_ip,
        .netmask=config->wifi_netmask,.gateway=config->wifi_gateway,.dns=config->wifi_dns};
    platform_wifi_ap_config_t ap = {.ssid=ap_ssid,.password=ap_password,
        .ip="192.168.65.204",.netmask="255.255.255.0",.channel=1,.max_clients=4};
    esp_err_t e = platform_wifi_start_apsta(&sta, &ap, NULL, NULL);
    if (e != ESP_OK) return e;
    s_started = true;
    s_ap_manual = false;
    s_ap_started_ms = esp_timer_get_time() / 1000LL;
    if (xTaskCreate(network_task, "gw_network", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t gateway_network_apply(const gateway_config_t *config)
{
    if (!config || !s_started) return ESP_ERR_INVALID_STATE;
    s_config = *config; s_active[0] = 0;
    char ap_ssid[33], ap_password[64];
    gateway_config_build_ap_identity(config->gateway_id, ap_ssid, sizeof(ap_ssid),
                                     ap_password, sizeof(ap_password));
    const platform_wifi_ap_config_t ap = {.ssid=ap_ssid,.password=ap_password,
        .ip="192.168.65.204",.netmask="255.255.255.0",.channel=1,.max_clients=4};
    (void)platform_wifi_apply_ap_config(&ap);
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

bool gateway_network_wifi_available(void) { return platform_wifi_sta_is_connected(); }
bool gateway_network_eth_uplink_available(void) { return s_config.eth_router_mode && bsp_eth_is_connected(); }
bool gateway_network_eth_debug_active(void) { return !s_config.eth_router_mode && bsp_eth_link_is_up(); }
bool gateway_network_production_state(bool wifi_available, bool eth_has_ip, bool eth_uplink_mode)
{
    return wifi_available || (eth_uplink_mode && eth_has_ip);
}
bool gateway_network_production_available(void)
{
    return gateway_network_production_state(platform_wifi_sta_is_connected(),
                                            bsp_eth_is_connected(),
                                            s_config.eth_router_mode);
}
bool gateway_network_is_connected(void) { return gateway_network_production_available(); }
esp_err_t gateway_network_set_ap(bool enabled)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;
    s_ap_manual = true;
    return enabled ? platform_wifi_ap_start() : platform_wifi_ap_stop();
}
bool gateway_network_ap_is_manual(void) { return s_ap_manual; }
const char *gateway_network_active_name(void) { return s_active; }
