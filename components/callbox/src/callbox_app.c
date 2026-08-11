/**
 * @file    callbox_app.c
 * @brief   Bootstrap sản phẩm Callbox SEWS — board Waveshare
 *          ESP32-S3-POE-ETH-8DI-8DO.
 *
 *          Trước Phase G.2, đây là thân app_main trong main/callbox_sews.c;
 *          sau khi di chuyển vào component callbox, hàm trở thành
 *          callbox_app_run() và main chỉ còn entrypoint ESP-IDF mỏng.
 *
 *          callbox_app_run tuần tự hóa toàn bộ quá trình khởi động thiết bị:
 *
 *          ═══ LUỒNG KHỞI ĐỘNG ═══
 *          ┌─────────────┐   ┌──────────────┐   ┌────────────────────┐
 *          │ NVS init    │ → │ Load config  │ → │ Tạo queue LED/     │
 *          │ (nvs_strg)  │   │ (factory     │   │ buzzer (20/10)\    │
 *          └─────────────┘   │  fallback)   │   └────────────────────┘
 *                            └──────────────┘
 *          ┌─────────────┐   ┌──────────────┐   ┌────────────────────┐
 *          │ BSP board   │ → │ LED/I/O/state│ → │ WiFi APSTA +       │
 *          │ init        │   │ init         │   │ netprt auto-save   │
 *          └─────────────┘   └──────────────┘   └────────────────────┘
 *          ┌─────────────┐   ┌─────────────┐   ┌────────────────────┐
 *          │ MQTT init   │ → │ 3 task      │ → │ vòng nền nhàn rỗi  │
 *          │ + 5s đợi    │   │ (io/state/  │   │                    │
 *          └─────────────┘   │ mqtt)       │   └────────────────────┘
 *                            └─────────────┘
 *
 *          Sequence service persist số thứ tự ngay khi cấp phát. Vòng lặp
 *          chính chỉ giữ composition root sống; các task chạy độc lập.
 *
 * @note    Các giá trị factory default (AGV1 và Robotics AUBOT 1 / 123456789 / broker …)
 *          chỉ được dùng khi NVS trống; người dùng có thể thay đổi qua
 *          portal cấu hình (config_portal).
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     callbox_config.h — hợp đồng cấu hình (Config_t)
 * @see     callbox_config_store.c — load/save cấu hình
 * @see     config_portal.c — portal cấu hình qua AP
 * @see     network_status_task.c — trạng thái LED/AP
 */
#include "callbox_app.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "callbox_config.h"
#include "io_handler.h"
#include "led_control.h"
#include "output_renderer.h"
#include "callbox_mqtt.h"
#include "state_machine.h"
#include "wifi_init.h"
#include "nvs_storage.h"
#include "callbox_config_store.h"
#include "config_portal.h"
#include "bsp_board.h"
#include "bsp_eth.h"
#include "network_status_task.h"
#include "sequence_service.h"
#include "app_event_queue.h"
#include "time_sync.h"

static const char *TAG = "MAIN";

/* Cấu hình tổng — local của composition root (callbox_app). Không còn khai báo
 * toàn cục: mọi consumer nhận cấu hình tường minh (config store, wifi_init,
 * config portal, time_sync, MQTT). */
static Config_t g_config = {
    /* Mặc định nhà máy dùng chung mọi thiết bị; ID và cài đặt có thể
     * thay đổi qua portal. */
    .wifi_ssid = "AGV1",
    .wifi_pass = "123456789",
    .wifi_profiles = {
        { .ssid = "AGV1", .password = "123456789" },
        { .ssid = "Robotics AUBOT 1", .password = "123456789" },
    },
    .wifi_profile_count = 2,
    .wifi_dhcp = true,
    .wifi_ip = "",
    .wifi_netmask = "",
    .wifi_gateway = "",
    .wifi_dns = "",
    .sntp_primary = "pool.ntp.org",
    .sntp_fallback = "time.google.com",
    .mqtt_broker = "10.1.201.13",
    .mqtt_port = 1884,
    .mqtt_transport = MQTT_TRANSPORT_TCP,
    .mqtt_user = "callbox",
    .mqtt_pass = "",
    .callbox_id = "001",
    /* Mật khẩu WebUI demo dùng chung theo yêu cầu vận hành hiện tại. */
    .web_password = "aubot",
};

/* Nhận diện định dạng mật khẩu tự sinh của các bản firmware trước.
 * Chỉ dùng để migrate về mật khẩu demo mới, không đụng đến mật khẩu tùy chỉnh. */
static bool is_legacy_generated_portal_password(const char *password)
{
    if (!password || strlen(password) != 14U ||
        strncmp(password, "Aubot-", 6U) != 0 ||
        password[12] != '-' || password[13] != '9') {
        return false;
    }

    for (size_t i = 6U; i < 12U; ++i) {
        const char c = password[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

/* Nạp cấu hình đã lưu từ NVS; chấp nhận hồi phục factory khi thiếu */
static bool load_config_from_nvs(void)
{
    esp_err_t err = callbox_config_store_load(&g_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load one or more saved configuration values: %s",
                 esp_err_to_name(err));
    }

    /* Khôi phục mặc định nhà máy nếu bản cũ lưu các cài đặt dùng chung trống. */
    if (g_config.wifi_profile_count == 0 || g_config.wifi_ssid[0] == '\0') {
        config_add_wifi_profile(&g_config, "Robotics AUBOT 1", "123456789");
        config_add_wifi_profile(&g_config, "AGV1", "123456789");
    }
    if (g_config.mqtt_broker[0] == '\0') {
        strncpy(g_config.mqtt_broker, "10.1.201.13", sizeof(g_config.mqtt_broker) - 1);
        g_config.mqtt_broker[sizeof(g_config.mqtt_broker) - 1] = '\0';
    }
    if (g_config.sntp_primary[0] == '\0') {
        strncpy(g_config.sntp_primary, "pool.ntp.org", sizeof(g_config.sntp_primary) - 1);
    }
    if (g_config.sntp_fallback[0] == '\0') {
        strncpy(g_config.sntp_fallback, "time.google.com", sizeof(g_config.sntp_fallback) - 1);
    }
    /* Chuẩn hóa cấu hình trống và mật khẩu tự sinh cũ về mật khẩu demo aubot.
     * Mật khẩu do người dùng tự đặt theo định dạng khác vẫn được giữ nguyên. */
    if (g_config.web_password[0] == '\0' ||
        is_legacy_generated_portal_password(g_config.web_password)) {
        strncpy(g_config.web_password, "aubot", sizeof(g_config.web_password) - 1);
        g_config.web_password[sizeof(g_config.web_password) - 1] = '\0';
        err = callbox_config_store_save(&g_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not persist default portal credential: %s",
                     esp_err_to_name(err));
            return false;
        }
        ESP_LOGW(TAG, "Portal credential migrated to demo default");
    }
    if (g_config.mqtt_port == 0) g_config.mqtt_port = 1884;
    /* Các bản cũ dùng ID như cb01. Chỉ giữ phần số để mọi thiết bị dùng
     * quy ước dễ thay thế AUBOT-Callbox-<số>. */
    char numeric_id[sizeof(g_config.callbox_id)] = { 0 };
    size_t numeric_len = 0;
    for (size_t i = 0; g_config.callbox_id[i] && numeric_len + 1 < sizeof(numeric_id); ++i) {
        if (g_config.callbox_id[i] >= '0' && g_config.callbox_id[i] <= '9') {
            numeric_id[numeric_len++] = g_config.callbox_id[i];
        }
    }
    if (numeric_len == 0) strncpy(numeric_id, "001", sizeof(numeric_id) - 1);
    strncpy(g_config.callbox_id, numeric_id, sizeof(g_config.callbox_id) - 1);
    g_config.callbox_id[sizeof(g_config.callbox_id) - 1] = '\0';
    return true;
}

/* Build the commissioning identity in the application layer.  The base name
 * remains the stable password, while the SSID adds a suffix from the factory
 * base MAC to distinguish boards which temporarily share one Callbox ID. */
static void build_configuration_ap_identity(const char *callbox_id,
                                            char *ap_ssid, size_t ap_ssid_size,
                                            char *ap_password, size_t ap_password_size)
{
    const char *id = (callbox_id && callbox_id[0]) ? callbox_id : "001";
    const int base_length = snprintf(ap_password, ap_password_size, "CALLBOX-%s", id);
    if (base_length < 0 || (size_t)base_length >= ap_password_size) {
        /* Config_t currently limits callbox_id to 15 bytes, so this only
         * protects future changes.  Keep boot deterministic and safe. */
        ESP_LOGW(TAG, "Callbox ID is too long for AP identity; using CALLBOX-001");
        (void)snprintf(ap_password, ap_password_size, "CALLBOX-001");
    }

    uint8_t mac[6] = { 0 };
    const esp_err_t mac_ret = esp_efuse_mac_get_default(mac);
    if (mac_ret != ESP_OK) {
        ESP_LOGW(TAG, "Cannot read factory base MAC (%s); using AP identity without suffix",
                 esp_err_to_name(mac_ret));
        (void)snprintf(ap_ssid, ap_ssid_size, "%s", ap_password);
        ESP_LOGI(TAG, "Configuration AP identity: %s", ap_ssid);
        return;
    }

    const int ssid_length = snprintf(ap_ssid, ap_ssid_size, "%s-%02X%02X%02X",
                                     ap_password, mac[3], mac[4], mac[5]);
    if (ssid_length < 0 || (size_t)ssid_length >= ap_ssid_size) {
        /* The configured 33-byte SSID buffer and numeric Callbox ID leave
         * enough space today.  If that invariant changes, prefer a usable
         * old-format AP over emitting a truncated hardware identity. */
        ESP_LOGW(TAG, "AP identity exceeds SSID buffer; using identity without MAC suffix");
        (void)snprintf(ap_ssid, ap_ssid_size, "%s", ap_password);
    }

    ESP_LOGI(TAG, "Device factory MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Configuration AP identity: %s", ap_ssid);
}

/* Lưu seq_num hiện tại xuống NVS (định kỳ trong vòng lặp chính) */
/* Hàm callback cho wifi_init: mở portal cấu hình ngay khi AP sẵn sàng */
static void start_config_portal_when_ap_is_ready(void)
{
    esp_err_t ret = config_portal_start(&g_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Configuration portal start failed: %s", esp_err_to_name(ret));
    }
}

void callbox_app_run(void)
{
    ESP_LOGI(TAG, "Starting Callbox SEWS Application");

    /* Bước 1: khởi tạo NVS (namespace "callbox"), chấp nhận erase khi hỏng */
    esp_err_t ret = nvs_storage_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return;
    }

    /* Bước 2: nạp cấu hình đã lưu (hoặc factory default) vào g_config */
    if (!load_config_from_nvs()) {
        ESP_LOGE(TAG, "Configuration credential migration failed; startup stopped");
        return;
    }

    /* Việc cấp phát số thứ tự độc lập với Config_t. NVS chỉ là phương tiện
     * lưu trữ do sequence_service sở hữu. */
    ret = sequence_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sequence service init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = app_event_queue_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Application event queue init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Bước 3: chuẩn bị queue lệnh buzzer nghiệp vụ (chủ sở hữu: led_control).
     * Nếu cấp phát thất bại → dừng ngay trước bsp_board_init(), cùng điểm
     * chết như baseline khi app_main tự tạo queue. */
    ret = led_control_prepare();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create buzzer queue: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Queues created successfully");

    /* Bước 4: khởi tạo board qua BSP (expander I2C + đầu vào số) */
    esp_err_t board_ret = bsp_board_init();
    if (board_ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP board init failed: %s", esp_err_to_name(board_ret));
    }

    /* Bước 5: khởi tạo các lớp phần mềm (LED, I/O, máy trạng thái) */
    led_control_init();
    output_renderer_init();
    io_handler_init();
    state_machine_init();
    ESP_LOGI(TAG, "Hardware initialized");

    /* Bước 6: Wi-Fi APSTA.  SSID carries a factory-MAC suffix for local
     * commissioning identity; password stays CALLBOX-<id>. */
    char ap_ssid[33];
    char ap_password[33];
    build_configuration_ap_identity(g_config.callbox_id, ap_ssid, sizeof(ap_ssid),
                                    ap_password, sizeof(ap_password));
    wifi_set_config_ap_callback(start_config_portal_when_ap_is_ready);
    /* Nối thông báo Rescue AP (từ wifi_init) tới phản hồi mạng (GPIO46 của
     * network_status_task) — đảo ngược dependency: CallBox không biết main. */
    wifi_set_rescue_ap_changed_callback(network_status_notify_rescue_ap_changed);
    esp_err_t wifi_ret = wifi_init_sta_profiles(&g_config, ap_ssid, ap_password);
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi STA/AP init failed: %s", esp_err_to_name(wifi_ret));
    }
    /* Chạy SNTP cho mọi chế độ mạng. Nó tự động thử lại tới khi có đường
     * mạng, để các sự kiện MQTT/TCP cũng nhận timestamp UTC thật thay vì
     * giây kể từ khi khởi động. */
    time_sync_init(&g_config);
    esp_err_t status_ret = network_status_task_start();
    if (status_ret != ESP_OK) {
        ESP_LOGE(TAG, "Network status task start failed: %s", esp_err_to_name(status_ret));
    }
    esp_err_t eth_ret = bsp_eth_init();
    if (eth_ret != ESP_OK) {
        ESP_LOGW(TAG, "W5500 Ethernet init failed: %s; continuing with Wi-Fi", esp_err_to_name(eth_ret));
    }
    /* Chờ 5 s cho Wi-Fi / Ethernet ổn định trước khi kéo MQTT */
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* Bước 7: khởi tạo MQTT (handshake CONTONT/SUBSCRIBE khi có mạng) */
    mqtt_client_init(&g_config);

    /* Bước 8: tạo 3 task chính chạy song song:
     *   - io_handler: đọc nút (debounding) → publish vào queue
     *   - state_machine: chuyển trạng thái, stall LED/buzzer/tower
     *   - mqtt_comm: gửi HEARTBEAT + reconnect
     * MQTT event callback do ESP-MQTT sở hữu và chỉ đẩy app_event vào queue.
     * Độ ưu tiên: state(10) > mqtt_comm(8) > io(5). */
    xTaskCreate(io_handler_task, "io_handler", 2048, NULL, 5, NULL);
    xTaskCreate(state_machine_task, "state_machine", 3072, NULL, 10, NULL);
    xTaskCreate(mqtt_communication_task, "mqtt_comm", 4096, NULL, 8, NULL);

    ESP_LOGI(TAG, "All tasks created and running");

    /* Sequence được persist khi cấp phát; vòng lặp chỉ giữ app sống. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
