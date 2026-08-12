/**
 * @file    gateway_app.c
 * @brief   Composition root cua firmware Gateway tren ESP32-S3.
 *
 * Composition root only. Dependency direction is:
 * BSP -> Laser protocol/profile -> Warehouse -> WebUI/MQTT.
 */
#include "gateway_app.h"

#include "bsp_can.h"
#include "bsp_board.h"
#include "bsp_eth.h"
#include "debug_http_server.h"
#include "esp_log.h"
#include "gateway_config.h"
#include "gateway_auth.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "gateway_output.h"
#include "gateway_status.h"
#include "laser_can_bringup.h"
#include "platform_wifi.h"
#include "platform_time.h"
#include "warehouse_manager.h"

static const char *TAG = "GATEWAY";

/* Cau hinh mang khoi dong cho Gateway.
 *
 * - STA van ket noi DHCP voi mang Robotics AUBOT 1 nhu firmware truoc.
 * - AP la kenh commissioning/doc debug doc lap, giu IP 192.168.65.204 de
 *   ky thuat vien khong phai doan dia chi moi khi chua co ha tang LAN.
 * - Day la product configuration cua Gateway, khong nam trong platform_wifi.
 */
esp_err_t gateway_app_run(void)
{
    esp_err_t err = gateway_config_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot load Gateway configuration: %s", esp_err_to_name(err));
        return err;
    }
    gateway_config_t config;
    gateway_config_get(&config);

    err = gateway_auth_init();
    if (err != ESP_OK) {
        /* Authentication/WebUI is an auxiliary plane. CAN, warehouse and MQTT
         * must keep running; protected routes fail closed until auth recovers. */
        ESP_LOGE(TAG, "Gateway accounts unavailable; protected WebUI disabled: %s",
                 esp_err_to_name(err));
    }

    err = bsp_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize board I/O and buzzer: %s", esp_err_to_name(err));
        return err;
    }

    err = gateway_output_start();
    if (err != ESP_OK) {
        /* gateway_output_start() has already attempted all-OFF fail-safe. */
        ESP_LOGE(TAG, "Physical outputs disabled in fail-safe OFF state: %s",
                 esp_err_to_name(err));
    }

    /* APSTA khong phu thuoc Ethernet hay CAN: khi LAN/CAN chua cam, ky thuat
     * vien van co the vao AP de phuc vu commissioning va debug sau nay. */
    err = gateway_network_start(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start Wi-Fi APSTA: %s", esp_err_to_name(err));
        return err;
    }

    /* W5500 tu lay DHCP va bao trang thai qua bsp_eth_is_connected().
     * Khong coi viec chua cam day la loi khoi dong Gateway. */
    const bsp_eth_network_config_t eth_network = config.eth_router_mode
        ? (bsp_eth_network_config_t) {
            .dhcp = config.eth_dhcp, .ip = config.eth_ip,
            .netmask = config.eth_netmask, .gateway = config.eth_gateway,
            .dns = config.eth_dns,
        }
        : (bsp_eth_network_config_t) {
            .dhcp = false, .ip = "169.254.1.1",
            .netmask = "255.255.0.0", .gateway = "0.0.0.0", .dns = NULL,
        };
    err = bsp_eth_init_with_config(&eth_network);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet W5500 unavailable: %s", esp_err_to_name(err));
    }

    const platform_time_config_t time_config = {
        .primary_server = config.sntp_primary,
        .fallback_server = config.sntp_fallback,
        .timezone = config.timezone,
    };
    err = platform_time_init(&time_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot start SNTP service: %s", esp_err_to_name(err));
    }

    /* CAN la kenh giao tiep chinh cua Gateway voi thiet bi hien truong. */
    err = bsp_can_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize CAN transport: %s", esp_err_to_name(err));
        return err;
    }

    /* Laser runtime owns discovery, Warn/status decode, timeout and explicit
     * configuration handshake. It never waits for HTTP or MQTT. */
    err = laser_can_bringup_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start laser CAN bring-up: %s", esp_err_to_name(err));
        return err;
    }

    err = warehouse_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize warehouse mappings: %s", esp_err_to_name(err));
        return err;
    }

    err = gateway_mqtt_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start MQTT publisher: %s", esp_err_to_name(err));
        return err;
    }

    err = debug_http_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start Ethernet debug dashboard: %s", esp_err_to_name(err));
        return err;
    }

    err = gateway_status_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start network/AP buzzer policy: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Gateway ready: group warehouse + laser runtime + local WebUI");
    return err;
}
