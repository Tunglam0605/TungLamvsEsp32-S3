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
#include "esp_system.h"
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
#include "bsp_do.h"
#include "bsp_eth.h"
#include "network_status_task.h"
#include "sequence_service.h"
#include "app_event_queue.h"
#include "health_monitor.h"
#include "callbox_storage_migration.h"
#include "time_sync.h"
#include "boot_validation.h"
#include "ota_boot_validator.h"
#include "ota_policy.h"
#include "ota_output_adapter.h"
#include "ota_service.h"
#include "ota_https_source.h"

static const char *TAG = "MAIN";
static volatile esp_err_t s_portal_start_result = ESP_ERR_INVALID_STATE;

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
    s_portal_start_result = ret;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Configuration portal start failed: %s", esp_err_to_name(ret));
    }

}

static esp_err_t wait_for_config_portal(uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    do {
        const esp_err_t result = s_portal_start_result;
        if (result == ESP_OK) return ESP_OK;
        if (result != ESP_ERR_INVALID_STATE) return result;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while ((int32_t)(deadline - xTaskGetTickCount()) > 0);
    return ESP_ERR_TIMEOUT;
}

/* Khởi tạo thất bại không được để thiết bị chạy zombie với
 * một phần task/queue bị thiếu. Tắt output nếu BSP đã sẵn sàng,
 * ghi nguyên nhân và restart sau khoảng ngắn. */
static void boot_fail_restart(const char *stage, esp_err_t error, bool board_ready)
{
    ESP_LOGE(TAG, "Fatal startup failure at %s: %s; restarting",
             stage, esp_err_to_name(error));
    const esp_err_t rollback_err = ota_boot_validator_handle_local_failure(stage);
    if (rollback_err != ESP_OK) {
        ESP_LOGE(TAG, "Pending-image rollback request failed at %s: %s; using controlled restart",
                 stage, esp_err_to_name(rollback_err));
    }
    /* Tắt ngay các output nếu BSP đã sẵn sàng. Sau thời gian
     * backoff xả log, tắt lần cuối ngay sát reset để một task đã
     * khởi tạo dở dang không thể để lại DO bật. */
    if (board_ready) (void)bsp_do_all_off();
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (board_ready) {
        const esp_err_t safe_err = bsp_do_all_off();
        if (safe_err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot force outputs OFF after startup failure: %s",
                     esp_err_to_name(safe_err));
        }
    }
    health_monitor_force_restart(stage);
}

/* Recovery breaker: after repeated local crashes, keep only the commissioning
 * path alive. No Mission/MQTT/output worker is created, so no stale business
 * heartbeat can reset this intentionally reduced runtime. The health monitor
 * clears the RTC streak and retries the full app after ten stable minutes. */
static void run_recovery_portal(void)
{
    ESP_LOGE(TAG, "Repeated local failures detected; starting AP/WebUI recovery mode");

    esp_err_t ret = nvs_storage_init();
    if (ret != ESP_OK) boot_fail_restart("recovery_nvs", ret, false);
    ret = callbox_storage_migrate();
    if (ret != ESP_OK) boot_fail_restart("recovery_nvs_migration", ret, false);
    if (!load_config_from_nvs()) {
        boot_fail_restart("recovery_config_load", ESP_FAIL, false);
    }

    char ap_ssid[33];
    char ap_password[33];
    build_configuration_ap_identity(g_config.callbox_id, ap_ssid, sizeof(ap_ssid),
                                    ap_password, sizeof(ap_password));
    s_portal_start_result = ESP_ERR_INVALID_STATE;
    ota_policy_set_mode(OTA_POLICY_MODE_RECOVERY);
    config_portal_set_recovery_mode(true);
    wifi_set_config_ap_callback(start_config_portal_when_ap_is_ready);
    ret = wifi_init_recovery_ap(ap_ssid, ap_password);
    if (ret != ESP_OK) boot_fail_restart("recovery_wifi", ret, false);

    ret = wait_for_config_portal(5000U);
    if (ret != ESP_OK) boot_fail_restart("recovery_config_portal", ret, false);

    ret = health_monitor_init(HEALTH_MONITOR_MODE_RECOVERY);
    if (ret != ESP_OK) boot_fail_restart("recovery_health_monitor", ret, false);
    ret = ota_boot_validator_start_recovery();
    if (ret != ESP_OK) boot_fail_restart("recovery_ota_qualification", ret, false);

    ESP_LOGW(TAG, "Recovery AP + WebUI verified ready at http://%s/; STA/SNTP/ETH/MQTT/I/O disabled",
             CALLBOX_AP_IP_ADDR);
    for (;;) vTaskDelay(pdMS_TO_TICKS(60000));
}

void callbox_app_run(void)
{
    ESP_LOGI(TAG, "Starting Callbox SEWS Application");

    const esp_err_t boot_validation_err = boot_validation_init();
    if (boot_validation_err != ESP_OK) {
        /* Do not pretend a failed state query is valid. A subsequent reset of
         * a pending image still lets the bootloader select the prior image. */
        ESP_LOGE(TAG, "Cannot inspect OTA boot state: %s", esp_err_to_name(boot_validation_err));
    }

    esp_err_t ota_runtime_err = ota_service_init();
    if (ota_runtime_err != ESP_OK) boot_fail_restart("ota_service", ota_runtime_err, false);
    ota_runtime_err = ota_https_source_init();
    if (ota_runtime_err != ESP_OK) boot_fail_restart("ota_https_source", ota_runtime_err, false);
    ota_runtime_err = ota_output_adapter_init();
    if (ota_runtime_err != ESP_OK) boot_fail_restart("ota_output_adapter", ota_runtime_err, false);
    ota_policy_set_mode(OTA_POLICY_MODE_NORMAL);

    health_monitor_boot_begin();
    if (health_monitor_recovery_requested()) run_recovery_portal();

    /* Bước 1: khởi tạo NVS (namespace "callbox"), chấp nhận erase khi hỏng */
    esp_err_t ret = nvs_storage_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        boot_fail_restart("nvs_storage", ret, false);
    }
    ret = callbox_storage_migrate();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS migration not verified: %s", esp_err_to_name(ret));
        boot_fail_restart("nvs_migration", ret, false);
    }

    /* Bước 2: nạp cấu hình đã lưu (hoặc factory default) vào g_config */
    if (!load_config_from_nvs()) {
        ESP_LOGE(TAG, "Configuration credential migration failed; startup stopped");
        boot_fail_restart("config_load", ESP_FAIL, false);
    }

    /* Việc cấp phát số thứ tự độc lập với Config_t. NVS chỉ là phương tiện
     * lưu trữ do sequence_service sở hữu. */
    ret = sequence_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sequence service init failed: %s", esp_err_to_name(ret));
        boot_fail_restart("sequence_service", ret, false);
    }

    ret = app_event_queue_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Application event queue init failed: %s", esp_err_to_name(ret));
        boot_fail_restart("app_event_queue", ret, false);
    }

    /* Bước 3: chuẩn bị queue lệnh buzzer nghiệp vụ (chủ sở hữu: led_control).
     * Nếu cấp phát thất bại → dừng ngay trước bsp_board_init(), cùng điểm
     * chết như baseline khi app_main tự tạo queue. */
    ret = led_control_prepare();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create buzzer queue: %s", esp_err_to_name(ret));
        boot_fail_restart("led_control_prepare", ret, false);
    }

    ESP_LOGI(TAG, "Queues created successfully");

    /* Bước 4: khởi tạo board qua BSP (expander I2C + đầu vào số) */
    esp_err_t board_ret = bsp_board_init();
    if (board_ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP board init failed: %s", esp_err_to_name(board_ret));
        boot_fail_restart("bsp_board", board_ret, false);
    }

    /* Bước 5: khởi tạo các lớp phần mềm (LED, I/O, máy trạng thái) */
    ret = led_control_init();
    if (ret != ESP_OK) boot_fail_restart("led_control", ret, true);
    ret = output_renderer_init();
    if (ret != ESP_OK) boot_fail_restart("output_renderer", ret, true);
    ret = io_handler_init();
    if (ret != ESP_OK) boot_fail_restart("io_handler", ret, true);
    state_machine_init();
    ESP_LOGI(TAG, "Hardware initialized");

    /* Bước 6: Wi-Fi APSTA.  SSID carries a factory-MAC suffix for local
     * commissioning identity; password stays CALLBOX-<id>. */
    char ap_ssid[33];
    char ap_password[33];
    build_configuration_ap_identity(g_config.callbox_id, ap_ssid, sizeof(ap_ssid),
                                    ap_password, sizeof(ap_password));
    config_portal_set_recovery_mode(false);
    wifi_set_config_ap_callback(start_config_portal_when_ap_is_ready);
    /* Nối thông báo Rescue AP (từ wifi_init) tới phản hồi mạng (GPIO46 của
     * network_status_task) — đảo ngược dependency: CallBox không biết main. */
    wifi_set_rescue_ap_changed_callback(network_status_notify_rescue_ap_changed);
    esp_err_t wifi_ret = wifi_init_sta_profiles(&g_config, ap_ssid, ap_password);
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi STA/AP init failed: %s", esp_err_to_name(wifi_ret));
        boot_fail_restart("wifi_init", wifi_ret, true);
    }
    /* Chạy SNTP cho mọi chế độ mạng. Nó tự động thử lại tới khi có đường
     * mạng, để các sự kiện MQTT/TCP cũng nhận timestamp UTC thật thay vì
     * giây kể từ khi khởi động. */
    time_sync_init(&g_config);
    esp_err_t status_ret = network_status_task_start();
    if (status_ret != ESP_OK) {
        ESP_LOGE(TAG, "Network status task start failed: %s", esp_err_to_name(status_ret));
        boot_fail_restart("network_status", status_ret, true);
    }
    esp_err_t eth_ret = bsp_eth_init();
    if (eth_ret != ESP_OK) {
        ESP_LOGW(TAG, "W5500 Ethernet init failed: %s; continuing with Wi-Fi", esp_err_to_name(eth_ret));
    }
    /* Chờ 5 s cho Wi-Fi / Ethernet ổn định trước khi kéo MQTT */
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* Bước 7: khởi tạo MQTT (handshake CONTONT/SUBSCRIBE khi có mạng) */
    ret = mqtt_client_init(&g_config);
    if (ret != ESP_OK) boot_fail_restart("mqtt_client", ret, true);

    /* Bước 8: tạo 3 task chính chạy song song:
     *   - io_handler: đọc nút (debounding) → publish vào queue
     *   - state_machine: chuyển trạng thái, stall LED/buzzer/tower
     *   - mqtt_comm: gửi HEARTBEAT + reconnect
     * MQTT event callback do ESP-MQTT sở hữu và chỉ đẩy app_event vào queue.
     * Độ ưu tiên: state(10) > mqtt_comm(8) > io(5). */
    if (xTaskCreate(io_handler_task, "io_handler", 2048, NULL, 5,
                    NULL) != pdPASS) {
        boot_fail_restart("io_handler_task", ESP_ERR_NO_MEM, true);
    }
    if (xTaskCreate(state_machine_task, "state_machine", 3072, NULL, 10,
                    NULL) != pdPASS) {
        boot_fail_restart("state_machine_task", ESP_ERR_NO_MEM, true);
    }
    if (xTaskCreate(mqtt_communication_task, "mqtt_comm", 4096, NULL, 8,
                    NULL) != pdPASS) {
        boot_fail_restart("mqtt_comm_task", ESP_ERR_NO_MEM, true);
    }

    ret = health_monitor_init(HEALTH_MONITOR_MODE_NORMAL);
    if (ret != ESP_OK) boot_fail_restart("health_monitor", ret, true);
    ret = ota_boot_validator_start_normal();
    if (ret != ESP_OK) boot_fail_restart("ota_qualification", ret, true);

    ESP_LOGI(TAG, "All tasks created and running");

    /* Sequence được persist khi cấp phát; vòng lặp chỉ giữ app sống. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
