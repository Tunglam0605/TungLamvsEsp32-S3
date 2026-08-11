/**
 * @file    gateway_app.c
 * @brief   Composition root cua firmware Gateway tren ESP32-S3.
 *
 * Gateway chi ghep cac dich vu chung cua board:
 *
 *   Wi-Fi AP + STA  ─┐
 *   Ethernet W5500 ─┼─> tang mang cua Gateway (MQTT/protocol se them sau)
 *   CAN transport  ─┘
 *
 * Module nay khong chua bat ky mapping nut nhan, den thap, Mission hay
 * Callbox portal nao. bsp_can chi la transport; protocol Laser/CAN se dat
 * trong component gateway khi co hop dong protocol chinh thuc.
 */
#include "gateway_app.h"

#include "bsp_can.h"
#include "bsp_eth.h"
#include "esp_log.h"
#include "platform_wifi.h"

static const char *TAG = "GATEWAY";

/* Cau hinh mang khoi dong cho Gateway.
 *
 * - STA van ket noi DHCP voi mang Robotics AUBOT 1 nhu firmware truoc.
 * - AP la kenh commissioning/doc debug doc lap, giu IP 192.168.65.204 de
 *   ky thuat vien khong phai doan dia chi moi khi chua co ha tang LAN.
 * - Day la product configuration cua Gateway, khong nam trong platform_wifi.
 */
static const platform_wifi_sta_network_config_t s_sta_network = {
    .dhcp = true,
};

static const platform_wifi_ap_config_t s_ap_config = {
    .ssid = "AUBOT-GATEWAY",
    .password = "gateway123",
    .ip = "192.168.65.204",
    .netmask = "255.255.255.0",
    .channel = 1,
    .max_clients = 4,
};

esp_err_t gateway_app_run(void)
{
    /* APSTA khong phu thuoc Ethernet hay CAN: khi LAN/CAN chua cam, ky thuat
     * vien van co the vao AP de phuc vu commissioning va debug sau nay. */
    esp_err_t err = platform_wifi_start_apsta(&s_sta_network, &s_ap_config,
                                              NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start Wi-Fi APSTA: %s", esp_err_to_name(err));
        return err;
    }

    err = platform_wifi_sta_set_credentials("Robotics AUBOT 1", "123456789");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot set Gateway STA credentials: %s", esp_err_to_name(err));
        return err;
    }
    err = platform_wifi_sta_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Gateway STA connect pending/failed: %s", esp_err_to_name(err));
    }

    /* W5500 tu lay DHCP va bao trang thai qua bsp_eth_is_connected().
     * Khong coi viec chua cam day la loi khoi dong Gateway. */
    err = bsp_eth_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet W5500 unavailable: %s", esp_err_to_name(err));
    }

    /* CAN la kenh giao tiep chinh cua Gateway voi thiet bi hien truong. */
    err = bsp_can_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize CAN transport: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Gateway ready: APSTA + Ethernet + CAN initialized");
    return err;
}
