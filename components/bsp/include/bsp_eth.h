/**
 * @file    bsp_eth.h
 * @brief   Giao diện Ethernet W5500 của board Waveshare ESP32-S3-POE-ETH-8DI-8DO.
 *
 *          W5500 là chip Ethernet tốc độ 10/100 Mbps, kết nối ESP32 qua SPI2:
 *
 *          ═══ SƠ ĐỒ CHÂN W5500 ═══
 *          ┌──────────┬──────────┬────────────────────────────────────┐
 *          │ Chân     │ GPIO     │ Mô tả                              │
 *          ├──────────┼──────────┼────────────────────────────────────┤
 *          │ INT      │ GPIO 12  │ Ngắt từ W5500                     │
 *          │ MOSI     │ GPIO 13  │ SPI data out (master → slave)      │
 *          │ MISO     │ GPIO 14  │ SPI data in (slave → master)       │
 *          │ SCLK     │ GPIO 15  │ SPI clock                          │
 *          │ CS       │ GPIO 16  │ Chip select                         │
 *          │ RESET    │ GPIO 39  │ Reset W5500 (kích hoạt boot)       │
 *          └──────────┴──────────┴────────────────────────────────────┘
 *
 *          SPI2_HOST, tốc độ 20 MHz. Hỗ trợ DHCP/nắm IP qua esp_netif,
 *          phát sự kiện ETHERNET_EVENT_CONNECTED/DISCONNECTED và
 *          IP_EVENT_ETH_GOT_IP.
 *
 * @note    Gọi sau esp_netif_init() và esp_event_loop_create_default().
 *          Board có thể dùng Wi-Fi hoặc Ethernet (bsp_eth_is_connected()).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_board.h — khởi tạo board tổng thể
 * @see     wifi_init.h — network_is_connected() dùng chung Wi-Fi + Ethernet
 */
#ifndef BSP_ETH_H
#define BSP_ETH_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gọi sau esp_netif_init() và esp_event_loop_create_default(). */
typedef struct {
    bool connected;      /* True only after an IP address is assigned. */
    char ip[16];
    char gateway[16];
} bsp_eth_status_t;

esp_err_t bsp_eth_init(void);
bool bsp_eth_is_connected(void);
void bsp_eth_get_status(bsp_eth_status_t *status);

/**
 * @brief Thử phục hồi đường Ethernet khi link vật lý vẫn còn nhưng DHCP/IP bị mất.
 *
 * Hàm có giới hạn tần suất nội bộ: bình thường chỉ khởi động lại DHCP client;
 * sau nhiều lần liên tiếp không lấy lại IP mới restart driver W5500. Khi link
 * đang down thật sự hàm không reset chip, nhờ đó dây bị rút không tạo reset loop.
 * Có thể gọi định kỳ từ network supervisor.
 */
esp_err_t bsp_eth_recover_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ETH_H */
