/**
 * @file    bsp_eth.c
 * @brief   Triển khai giao diện Ethernet W5500 cho board Waveshare.
 *
 *          W5500 được điều khiển qua SPI2 host, 20 MHz. Có một reset GPIO
 *          (GPIO 39) được nhấn mạnh khi khởi tạo.
 *
 *          ═══ SƠ ĐỒ CHÂN ═══
 *          ┌──────────┬──────────┬──────────────────────────────┐
 *          │ INT      │ GPIO 12  │ Ngắt W5500                   │
 *          │ MOSI     │ GPIO 13  │ SPI MOSI                     │
 *          │ MISO     │ GPIO 14  │ SPI MISO                     │
 *          │ SCLK     │ GPIO 15  │ SPI clock                    │
 *          │ CS       │ GPIO 16  │ Chip select                  │
 *          │ RESET    │ GPIO 39  │ Reset (hạ thấp 10ms, nhả)    │
 *          └──────────┴──────────┴──────────────────────────────┘
 *
 * @note    Đăng ký sự kiện ETHERNET_EVENT và IP_EVENT_ETH_GOT_IP để cập
 *          nhật s_eth_has_ip. bsp_eth_is_connected() phản ánh trạng thái IP.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     bsp_eth.h — API Ethernet
 * @see     wifi_init.c — network_is_connected() dùng chung Wi-Fi + Ethernet
 */
#include "bsp_eth.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_ETH";

#define BSP_ETH_INT_GPIO  GPIO_NUM_12
#define BSP_ETH_MOSI_GPIO GPIO_NUM_13
#define BSP_ETH_MISO_GPIO GPIO_NUM_14
#define BSP_ETH_SCLK_GPIO GPIO_NUM_15
#define BSP_ETH_CS_GPIO   GPIO_NUM_16
#define BSP_ETH_RST_GPIO  GPIO_NUM_39
#define BSP_ETH_SPI_HOST  SPI2_HOST
#define BSP_ETH_SPI_HZ    (20 * 1000 * 1000)

static esp_eth_handle_t s_eth_handle;
static bool s_eth_initialized;
static bool s_eth_has_ip;
static char s_eth_ip[16];
static char s_eth_gateway[16];

/*
 * Validate the factory-derived address before exposing it to the LAN.  A
 * zero or multicast address is invalid as a source MAC and would prevent a
 * normal DHCP exchange on many managed switches.
 */
static bool eth_mac_is_valid(const uint8_t mac[6])
{
    static const uint8_t zero_mac[6] = {0};

    return memcmp(mac, zero_mac, sizeof(zero_mac)) != 0 &&
           (mac[0] & 0x01U) == 0;
}

/*
 * Xử lý sự kiện liên kết Ethernet:
 *  - CONNECTED: dây đã cắm và liên kết ổn định (chưa chắc có IP)
 *  - DISCONNECTED: mất dây/liên kết → chắc chắn mất IP, reset cờ.
 */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "W5500 link up");
    } else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
        s_eth_has_ip = false;
        s_eth_ip[0] = '\0';
        s_eth_gateway[0] = '\0';
        ESP_LOGW(TAG, "W5500 link down");
    }
}

/*
 * Xử lý khi Ethernet có IP (sự kiện IP_EVENT_ETH_GOT_IP).
 * Đây là dấu hiệu duy nhất để báo "mạng Ethernet sẵn sàng".
 */
static void eth_got_ip_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    s_eth_has_ip = true;
    snprintf(s_eth_ip, sizeof(s_eth_ip), IPSTR, IP2STR(&event->ip_info.ip));
    snprintf(s_eth_gateway, sizeof(s_eth_gateway), IPSTR, IP2STR(&event->ip_info.gw));
    ESP_LOGI(TAG, "W5500 got IP: " IPSTR, IP2STR(&event->ip_info.ip));
}

/*
 * Reset phần cứng W5500 bằng xung thấp:
 *   1) Cấu hình chân RST là output
 *   2) Hạ xuống 0 (assert) trong 10ms
 *   3) Nâng lên 1 (release) rồi đợi 100ms cho chip ổn định
 * Đây là bước bắt buộc trước khi SPI giao tiếp với W5500.
 */
static esp_err_t w5500_reset(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_ETH_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "configure W5500 reset pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_ETH_RST_GPIO, 0), TAG, "assert W5500 reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_ETH_RST_GPIO, 1), TAG, "release W5500 reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t bsp_eth_init(void)
{
    /* Bảo vệ: nếu driver đã cài đặt thì không khởi tạo lại (tránh hỏng SPI). */
    if (s_eth_handle != NULL) {
        return s_eth_initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;
    esp_eth_mac_t *mac = NULL;
    esp_eth_phy_t *phy = NULL;
    esp_eth_handle_t eth_handle = NULL;
    esp_netif_t *netif = NULL;
    esp_eth_netif_glue_handle_t netif_glue = NULL;
    bool isr_service_owned = false;
    bool spi_bus_owned = false;
    bool driver_installed = false;
    bool eth_handler_registered = false;
    bool ip_handler_registered = false;
    bool start_attempted = false;

    s_eth_has_ip = false;
    s_eth_ip[0] = '\0';
    s_eth_gateway[0] = '\0';

    /* BƯỚC 1 — Reset W5500 về trạng thái sạch. */
    ESP_GOTO_ON_ERROR(w5500_reset(), fail, TAG, "reset W5500");

    /* BƯỚC 2 — Cài dịch vụ ISR cho GPIO (chân INT của W5500 cần ngắt).
     * Nếu đã cài ở nơi khác (ESP_ERR_INVALID_STATE) thì vẫn chấp nhận. */
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "install GPIO ISR service: %s", esp_err_to_name(ret));
        goto fail;
    }
    isr_service_owned = ret == ESP_OK;

    /*
     * BƯỚC 3 — Khởi tạo bus SPI 2 với 3 dây MOSI/MISO/SCLK.
     * quadwp/quadhd = -1: không dùng chế độ 4-dây (chỉ SPI mode 0, 3 dây).
     * DMA tự chọn (SPI_DMA_CH_AUTO) để truyền khối hiệu quả.
     */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_ETH_MOSI_GPIO,
        .miso_io_num = BSP_ETH_MISO_GPIO,
        .sclk_io_num = BSP_ETH_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(BSP_ETH_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                      fail, TAG, "initialize W5500 SPI bus");
    spi_bus_owned = true;

    /*
     * BƯỚC 4 — Cấu hình thiết bị SPI trên bus (CS = GPIO 16, 20 MHz, mode 0).
     * queue_size=20: độ sâu hàng đợi giao dịch SPI.
     */
    spi_device_interface_config_t dev_cfg = {
        .mode = 0,
        .clock_speed_hz = BSP_ETH_SPI_HZ,
        .spics_io_num = BSP_ETH_CS_GPIO,
        .queue_size = 20,
    };
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(BSP_ETH_SPI_HOST, &dev_cfg);
    w5500_cfg.base.int_gpio_num = BSP_ETH_INT_GPIO;

    /* Cấu hình MAC/PHY mặc định; PHY không dùng chân reset riêng. */
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = -1;

    /* Tạo đối tượng MAC và PHY cho W5500 (driver esp_eth). */
    mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    phy = esp_eth_phy_new_w5500(&phy_cfg);
    ESP_GOTO_ON_FALSE(mac != NULL && phy != NULL, ESP_ERR_NO_MEM, fail, TAG,
                      "create W5500 MAC/PHY");

    /* BƯỚC 5 — Cài đặt driver esp_eth với MAC+PHY đã tạo. */
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_GOTO_ON_ERROR(esp_eth_driver_install(&eth_cfg, &eth_handle), fail, TAG,
                      "install W5500 driver");
    driver_installed = true;

    /*
     * W5500 has no factory MAC of its own.  ESP-IDF derives ESP_MAC_ETH from
     * the ESP32-S3 factory base MAC; assign it before attaching/starting the
     * driver so Ethernet never sends DHCP using 00:00:00:00:00:00.
     */
    uint8_t eth_mac[6] = {0};
    ESP_GOTO_ON_ERROR(esp_read_mac(eth_mac, ESP_MAC_ETH), fail, TAG,
                      "read Ethernet MAC");
    if (!eth_mac_is_valid(eth_mac)) {
        ESP_LOGE(TAG, "invalid derived W5500 MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 eth_mac[0], eth_mac[1], eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);
        ret = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    ESP_GOTO_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac), fail, TAG,
                      "set W5500 MAC");
    ESP_LOGI(TAG, "W5500 MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             eth_mac[0], eth_mac[1], eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);

    /*
     * BƯỚC 6 — Gắn Ethernet vào esp-netif (tầng TCP/IP).
     * esp_netif_attach + glue sẽ đưa gói tin W5500 vào stack LWIP.
     */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    netif = esp_netif_new(&netif_cfg);
    ESP_GOTO_ON_FALSE(netif != NULL, ESP_ERR_NO_MEM, fail, TAG,
                      "create W5500 netif");
    netif_glue = esp_eth_new_netif_glue(eth_handle);
    ESP_GOTO_ON_FALSE(netif_glue != NULL, ESP_ERR_NO_MEM, fail, TAG,
                      "create W5500 netif glue");
    ESP_GOTO_ON_ERROR(esp_netif_attach(netif, netif_glue), fail, TAG,
                      "attach W5500 netif");

    /* BƯỚC 7 — Đăng ký sự kiện link và sự kiện có IP. */
    ESP_GOTO_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                                 eth_event_handler, NULL), fail, TAG,
                      "register Ethernet event handler");
    eth_handler_registered = true;
    ESP_GOTO_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                                 eth_got_ip_handler, NULL), fail, TAG,
                      "register Ethernet IP handler");
    ip_handler_registered = true;

    /* BƯỚC 8 — Bắt đầu hoạt động Ethernet. */
    start_attempted = true;
    ESP_GOTO_ON_ERROR(esp_eth_start(eth_handle), fail, TAG, "start W5500");

    /* Chỉ công bố handle sau khi mọi bước khởi tạo đã thành công. */
    s_eth_handle = eth_handle;
    s_eth_initialized = true;

    ESP_LOGI(TAG, "W5500 initialized (SPI2: INT=%d MOSI=%d MISO=%d SCLK=%d CS=%d RST=%d)",
             BSP_ETH_INT_GPIO, BSP_ETH_MOSI_GPIO, BSP_ETH_MISO_GPIO,
             BSP_ETH_SCLK_GPIO, BSP_ETH_CS_GPIO, BSP_ETH_RST_GPIO);
    return ESP_OK;

fail:
    s_eth_handle = NULL;
    s_eth_initialized = false;
    s_eth_has_ip = false;
    s_eth_ip[0] = '\0';
    s_eth_gateway[0] = '\0';

    if (start_attempted && eth_handle) {
        const esp_err_t cleanup_err = esp_eth_stop(eth_handle);
        if (cleanup_err != ESP_OK && cleanup_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "rollback: stop W5500 failed: %s", esp_err_to_name(cleanup_err));
        }
    }
    if (ip_handler_registered) {
        const esp_err_t cleanup_err = esp_event_handler_unregister(
            IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_got_ip_handler);
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(TAG, "rollback: unregister IP handler failed: %s",
                     esp_err_to_name(cleanup_err));
        }
    }
    if (eth_handler_registered) {
        const esp_err_t cleanup_err = esp_event_handler_unregister(
            ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler);
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(TAG, "rollback: unregister Ethernet handler failed: %s",
                     esp_err_to_name(cleanup_err));
        }
    }
    if (netif_glue) {
        const esp_err_t cleanup_err = esp_eth_del_netif_glue(netif_glue);
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(TAG, "rollback: delete netif glue failed: %s",
                     esp_err_to_name(cleanup_err));
        }
    }
    if (driver_installed) {
        const esp_err_t cleanup_err = esp_eth_driver_uninstall(eth_handle);
        if (cleanup_err == ESP_OK) {
            driver_installed = false;
        } else {
            ESP_LOGE(TAG, "rollback: uninstall W5500 driver failed: %s",
                     esp_err_to_name(cleanup_err));
            /* Preserve the handle as a poison guard: retrying initialization
             * over a live driver would corrupt SPI/ISR ownership. */
            s_eth_handle = eth_handle;
        }
    }
    if (netif) {
        esp_netif_destroy(netif);
    }
    /* The driver references MAC/PHY until uninstall succeeds. Avoid a
     * use-after-free if an unexpected driver state prevents uninstall. */
    if (!driver_installed) {
        if (phy) {
            const esp_err_t cleanup_err = phy->del(phy);
            if (cleanup_err != ESP_OK) {
                ESP_LOGW(TAG, "rollback: delete W5500 PHY failed: %s",
                         esp_err_to_name(cleanup_err));
            }
        }
        if (mac) {
            const esp_err_t cleanup_err = mac->del(mac);
            if (cleanup_err != ESP_OK) {
                ESP_LOGW(TAG, "rollback: delete W5500 MAC failed: %s",
                         esp_err_to_name(cleanup_err));
            }
        }
        if (spi_bus_owned) {
            const esp_err_t cleanup_err = spi_bus_free(BSP_ETH_SPI_HOST);
            if (cleanup_err != ESP_OK) {
                ESP_LOGW(TAG, "rollback: free W5500 SPI bus failed: %s",
                         esp_err_to_name(cleanup_err));
            }
        }
    }
    if (isr_service_owned && !driver_installed) {
        gpio_uninstall_isr_service();
    }
    return ret;
}

bool bsp_eth_is_connected(void)
{
    return s_eth_has_ip;
}

void bsp_eth_get_status(bsp_eth_status_t *status)
{
    if (status == NULL) return;

    status->connected = s_eth_has_ip;
    snprintf(status->ip, sizeof(status->ip), "%s", s_eth_ip);
    snprintf(status->gateway, sizeof(status->gateway), "%s", s_eth_gateway);
}
