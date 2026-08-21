/**
 * @file    mqtt_client.c
 * @brief   Bộ điều hợp vận chuyển MQTT cho Callbox.
 *
 * Giao thức ứng dụng vẫn là callbox/<id>/{cmd,event,status}. Module này
 * ủy thác khung MQTT (framing), keep-alive, reconnect và TLS cho ESP-MQTT
 * để cùng một firmware chạy được với broker TCP nội bộ hoặc broker TLS công
 * cộng.
 *
 * Bộ điều hợp là người tiêu thụ CHỈ ĐỌC telemetry: trạng thái mission/comm
 * cho heartbeat đọc từ status store của CallBox (status.Mission / status.CommState)
 * thay vì gọi Mission Manager — MQTT không bao giờ sửa status store.
 */
#include "callbox_mqtt.h"

#include "sdkconfig.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "app_event_queue.h"
#include "network_link.h"
#include "protocol_types.h"
#include "status.h"
#include "wifi_init.h"
#include "time_sync.h"
#include "platform_ota.h"

#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "MQTT_CLIENT";

/* Ảnh cấu hình riêng của MQTT (lifetime-safe): sao chép có giới hạn các
 * trường mà adapter cần; KHÔNG giữ con trỏ vào Config_t của caller — portal
 * có thể sửa đổi Config_t khi chạy. */
typedef struct {
    char broker[64];
    uint16_t port;
    MqttTransport_t transport;
    char user[32];
    char pass[64];
    char callbox_id[16];
} mqtt_runtime_config_t;

static mqtt_runtime_config_t s_config;

static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_client_mutex;
static volatile bool s_mqtt_connected;
static volatile bool s_client_started;
static QueueHandle_t s_publish_queue;
static bool s_auth_config_error_logged;

/* Sao chép giới hạn, luôn kết thúc '\0'. Không lưu con trỏ vào caller. */
static void mqtt_copy_string(char *dst, size_t dst_size, const char *src)
{
    strncpy(dst, src ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void mqtt_copy_runtime_config(const Config_t *config)
{
    if (!config) return;
    mqtt_copy_string(s_config.broker, sizeof(s_config.broker), config->mqtt_broker);
    s_config.port = config->mqtt_port;
    s_config.transport = config->mqtt_transport;
    mqtt_copy_string(s_config.user, sizeof(s_config.user), config->mqtt_user);
    mqtt_copy_string(s_config.pass, sizeof(s_config.pass), config->mqtt_pass);
    mqtt_copy_string(s_config.callbox_id, sizeof(s_config.callbox_id), config->callbox_id);
    s_auth_config_error_logged = false;
}

static bool mqtt_credentials_allowed(void)
{
#if defined(CONFIG_CALLBOX_ALLOW_ANONYMOUS_MQTT) && CONFIG_CALLBOX_ALLOW_ANONYMOUS_MQTT
    return true;
#else
    return s_config.user[0] != '\0' && s_config.pass[0] != '\0';
#endif
}

static void mqtt_publish_worker(void *pvParameters)
{
    (void)pvParameters;
    MQTTMsg_t message;

    for (;;) {
        if (xQueueReceive(s_publish_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Chỉ worker ưu tiên thấp này mới được chạm vào API TX của ESP-MQTT.
         * Nếu broker/outbox bị chặn, các task nút bấm/trạng thái vẫn được
         * lập lịch bình thường. */
        if (!s_client_mutex ||
            xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            ESP_LOGW(TAG, "MQTT TX skipped: client busy (%s)", message.topic);
            continue;
        }

        if (s_client && s_mqtt_connected) {
            const int message_id = esp_mqtt_client_enqueue(
                s_client, message.topic, message.payload, 0, message.qos,
                message.retain, true);
            if (message_id < 0) {
                ESP_LOGW(TAG, "MQTT TX enqueue failed for %s (id=%d)",
                         message.topic, message_id);
            }
        }
        xSemaphoreGive(s_client_mutex);
    }
}

/* ESP-MQTT giữ các con trỏ này suốt vòng đời client. Sao chép mọi giá trị
 * hiển thị cho người dùng trước khi mở kết nối để save/reconfigure không thể
 * để tầng vận chuyển tham chiếu tới cấu hình đã bị sửa đổi. */
static char s_uri[160];
static char s_client_id[64];
static char s_username[32];
static char s_password[64];
static char s_status_topic[96];

static const char *mqtt_transport_name(MqttTransport_t transport)
{
    return transport == MQTT_TRANSPORT_TLS ? "TLS" : "TCP";
}

/* Chuỗi trạng thái wire của WCS (chữ thường) — đọc từ status store, không
 * phụ thuộc Mission Manager. */
static const char *mqtt_task_state_name(TaskState_t state)
{
    switch (state) {
    case TASK_QUEUED: return "queued";
    case TASK_ASSIGNED: return "assigned";
    case TASK_LOCKED: return "locked";
    case TASK_COMPLETED: return "completed";
    default: return "idle";
    }
}

static const char *mqtt_comm_state_name(comm_state_t state)
{
    switch (state) {
    case COMM_SYNCING: return "syncing";
    case COMM_READY: return "ready";
    default: return "offline";
    }
}

static void mqtt_ota_progress_callback(int percent, size_t written, size_t total, void *ctx)
{
    (void)ctx;
    char topic[96];
    snprintf(topic, sizeof(topic), "callbox/%s/ota/status", s_config.callbox_id);
    char payload[160];
    if (percent < 0) {
        snprintf(payload, sizeof(payload), "{\"status\":\"failed\",\"progress\":-1}");
    } else if (percent >= 100) {
        snprintf(payload, sizeof(payload), "{\"status\":\"rebooting\",\"progress\":100}");
    } else {
        snprintf(payload, sizeof(payload), "{\"status\":\"downloading\",\"progress\":%d,\"written\":%u,\"total\":%u}",
                 percent, (unsigned)written, (unsigned)total);
    }
    MQTTMsg_t msg = {0};
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.qos = 1;
    msg.retain = false;
    if (s_publish_queue) {
        xQueueSend(s_publish_queue, &msg, 0);
    }
}

static void mqtt_handle_ota_command(const char *payload)
{
    const char *url_key = strstr(payload, "\"url\":");
    if (!url_key) url_key = strstr(payload, "\"url\" :");
    if (!url_key) {
        ESP_LOGW(TAG, "OTA command missing 'url' field");
        return;
    }
    const char *quote1 = strchr(url_key + 6, '\"');
    if (!quote1) return;
    const char *quote2 = strchr(quote1 + 1, '\"');
    if (!quote2) return;

    char url[256] = {0};
    size_t url_len = quote2 - quote1 - 1;
    if (url_len >= sizeof(url)) url_len = sizeof(url) - 1;
    strncpy(url, quote1 + 1, url_len);
    url[url_len] = '\0';

    if (status.Mission[0] != TASK_IDLE || status.Mission[1] != TASK_IDLE) {
        ESP_LOGW(TAG, "Rejecting OTA upgrade: missions currently in progress");
        char topic[96];
        snprintf(topic, sizeof(topic), "callbox/%s/ota/status", s_config.callbox_id);
        MQTTMsg_t msg = {0};
        strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
        strcpy(msg.payload, "{\"status\":\"rejected\",\"reason\":\"mission_active\"}");
        msg.qos = 1;
        if (s_publish_queue) xQueueSend(s_publish_queue, &msg, 0);
        return;
    }

    ESP_LOGI(TAG, "Starting Fleet Remote OTA upgrade from URL: %s", url);
    esp_err_t err = platform_ota_start_from_url(url, mqtt_ota_progress_callback, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate URL OTA: %s", esp_err_to_name(err));
        char topic[96];
        snprintf(topic, sizeof(topic), "callbox/%s/ota/status", s_config.callbox_id);
        MQTTMsg_t msg = {0};
        strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
        snprintf(msg.payload, sizeof(msg.payload) - 1, "{\"status\":\"error\",\"error\":\"%s\"}", esp_err_to_name(err));
        msg.qos = 1;
        if (s_publish_queue) xQueueSend(s_publish_queue, &msg, 0);
    }
}

static void mqtt_handle_command(const char *topic, const char *payload)
{
    char ota_specific_topic[96];
    snprintf(ota_specific_topic, sizeof(ota_specific_topic), "callbox/%s/cmd/ota", s_config.callbox_id);

    if (strcmp(topic, ota_specific_topic) == 0 ||
        strcmp(topic, "callbox/all/cmd/ota") == 0 ||
        strstr(payload, "\"cmd\":\"ota_upgrade\"") != NULL ||
        strstr(payload, "\"cmd\": \"ota_upgrade\"") != NULL) {
        mqtt_handle_ota_command(payload);
        return;
    }

    char expected_topic[96];
    snprintf(expected_topic, sizeof(expected_topic), MQTT_CMD_TOPIC, s_config.callbox_id);
    if (strcmp(topic, expected_topic) != 0) {
        ESP_LOGD(TAG, "Ignoring command on unexpected topic: %s", topic);
        return;
    }

    app_event_t app_event = { .type = APP_EVENT_WCS_COMMAND };
    if (!protocol_parse_command_json(payload, &app_event.command)) {
        ESP_LOGW(TAG, "Ignoring invalid command JSON");
        return;
    }
    if (!app_event_send(&app_event, 0)) {
        ESP_LOGW(TAG, "Application event queue full; dropping WCS command");
        return;
    }
    ESP_LOGI(TAG, "Queued WCS cmd=%s task=%d ref_seq=%lu",
             protocol_command_name(app_event.command.type), app_event.command.task,
             (unsigned long)app_event.command.ref_seq);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_mqtt_connected = true;
        char topic[96];
        snprintf(topic, sizeof(topic), MQTT_CMD_TOPIC, s_config.callbox_id);
        const int message_id = esp_mqtt_client_subscribe(event->client, topic, MQTT_QoS);

        char ota_topic[96];
        snprintf(ota_topic, sizeof(ota_topic), "callbox/%s/cmd/ota", s_config.callbox_id);
        esp_mqtt_client_subscribe(event->client, ota_topic, MQTT_QoS);
        esp_mqtt_client_subscribe(event->client, "callbox/all/cmd/ota", MQTT_QoS);

        const app_event_t app_event = { .type = APP_EVENT_MQTT_CONNECTED };
        (void)app_event_send(&app_event, 0);
        ESP_LOGI(TAG, "Connected via %s; subscribed %s, %s, callbox/all/cmd/ota (id=%d)",
                 mqtt_transport_name(s_config.transport), topic, ota_topic, message_id);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        { const app_event_t app_event = { .type = APP_EVENT_MQTT_DISCONNECTED };
          (void)app_event_send(&app_event, 0); }
        ESP_LOGW(TAG, "Disconnected; ESP-MQTT will retry while network is available");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Command subscription accepted (id=%d)", event->msg_id);
        break;

    case MQTT_EVENT_DATA: {
        /* Lệnh Callbox cố ý nhỏ. Không phân tích payload bị phân mảnh như
         * lệnh hoàn chỉnh; payload vượt kích thước bị bỏ qua. */
        if (event->current_data_offset != 0 || !event->topic || event->topic_len <= 0 ||
            event->topic_len >= 160 || event->data_len >= 512) {
            ESP_LOGW(TAG, "Ignoring fragmented or oversized MQTT command");
            break;
        }
        char topic[160] = {0};
        char payload[512] = {0};
        memcpy(topic, event->topic, (size_t)event->topic_len);
        if (event->data && event->data_len > 0) {
            memcpy(payload, event->data, (size_t)event->data_len);
        }
        payload[event->data_len] = '\0';
        mqtt_handle_command(topic, payload);
        break;
    }
    case MQTT_EVENT_ERROR:
        s_mqtt_connected = false;
        if (event->error_handle) {
            ESP_LOGE(TAG, "Connection error type=%d tls_err=%s verify=0x%x socket=%d",
                     event->error_handle->error_type,
                     esp_err_to_name(event->error_handle->esp_tls_last_esp_err),
                     event->error_handle->esp_tls_cert_verify_flags,
                     event->error_handle->esp_transport_sock_errno);
        } else {
            ESP_LOGE(TAG, "MQTT connection error");
        }
        break;
    default:
        break;
    }
}

static void mqtt_destroy_locked(void)
{
    if (!s_client) return;
    (void)esp_mqtt_client_stop(s_client);
    (void)esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_client_started = false;
    s_mqtt_connected = false;
}

void mqtt_client_init(const Config_t *config)
{
    if (!config) return;

    /* Chụp ảnh cấu hình MQTT ngay trước khi dựng runtime: nếu portal đã lưu
     * trước đó (MQTT khởi tạo muộn), snapshot này đã mang cấu hình mới nhất. */
    mqtt_copy_runtime_config(config);

    s_client_mutex = xSemaphoreCreateMutex();
    if (!s_client_mutex) {
        ESP_LOGE(TAG, "Cannot create MQTT synchronization mutex");
        return;
    }
    s_publish_queue = xQueueCreate(24, sizeof(MQTTMsg_t));
    if (!s_publish_queue) {
        ESP_LOGE(TAG, "Cannot create MQTT TX queue");
        return;
    }
    if (xTaskCreate(mqtt_publish_worker, "mqtt_tx", 3072, NULL, 7, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create MQTT TX task");
        vQueueDelete(s_publish_queue);
        s_publish_queue = NULL;
        return;
    }
    ESP_LOGI(TAG, "ESP-MQTT ready: TCP internal and TLS public broker modes");
}

void mqtt_client_subscribe_cmd(void)
{
    if (!s_client || !s_mqtt_connected) return;
    char topic[96];
    snprintf(topic, sizeof(topic), MQTT_CMD_TOPIC, s_config.callbox_id);
    (void)esp_mqtt_client_subscribe(s_client, topic, MQTT_QoS);
}

void mqtt_client_connect(void)
{
    if (!s_client_mutex || !network_link_is_connected() ||
        !s_config.broker[0] || s_config.port == 0) {
        return;
    }
    if (!mqtt_credentials_allowed()) {
        if (!s_auth_config_error_logged) {
            ESP_LOGE(TAG, "MQTT connection blocked: username and password are required");
            s_auth_config_error_logged = true;
        }
        return;
    }
    if (s_config.transport == MQTT_TRANSPORT_TLS && !time_sync_is_valid()) {
        ESP_LOGI(TAG, "TLS selected: waiting for SNTP time synchronization");
        return;
    }

    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    if (s_client_started) {
        xSemaphoreGive(s_client_mutex);
        return;
    }

    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u",
             s_config.transport == MQTT_TRANSPORT_TLS ? "mqtts" : "mqtt",
             s_config.broker, (unsigned)s_config.port);
    snprintf(s_client_id, sizeof(s_client_id), CALLBOX_DEVICE_NAME_PREFIX "%s",
             s_config.callbox_id);
    snprintf(s_status_topic, sizeof(s_status_topic), MQTT_STATUS_TOPIC, s_config.callbox_id);
    strncpy(s_username, s_config.user, sizeof(s_username) - 1);
    s_username[sizeof(s_username) - 1] = '\0';
    strncpy(s_password, s_config.pass, sizeof(s_password) - 1);
    s_password[sizeof(s_password) - 1] = '\0';

    esp_mqtt_client_config_t config = {
        .broker.address.uri = s_uri,
        .credentials.username = s_username[0] ? s_username : NULL,
        .credentials.authentication.password = s_password[0] ? s_password : NULL,
        .credentials.client_id = s_client_id,
        .session.keepalive = 30,
        .session.last_will.topic = s_status_topic,
        .session.last_will.msg = "{\"online\":false}",
        .session.last_will.qos = MQTT_QoS,
        .session.last_will.retain = true,
        .network.timeout_ms = 10000,
        .network.reconnect_timeout_ms = 5000,
    };
    if (s_config.transport == MQTT_TRANSPORT_TLS) {
        /* Bắt buộc xác thực CA công cộng: không bao giờ tắt kiểm tra hostname/CN. */
        config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    s_client = esp_mqtt_client_init(&config);
    if (!s_client) {
        ESP_LOGE(TAG, "Could not create ESP-MQTT client for %s", s_uri);
        xSemaphoreGive(s_client_mutex);
        return;
    }
    (void)esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    const esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start MQTT client: %s", esp_err_to_name(err));
        mqtt_destroy_locked();
    } else {
        s_client_started = true;
        ESP_LOGI(TAG, "Connecting %s to %s as %s", mqtt_transport_name(s_config.transport),
                 s_uri, s_client_id);
    }
    xSemaphoreGive(s_client_mutex);
}

void mqtt_client_reconfigure(const Config_t *config)
{
    if (!config) return;
    if (!s_client_mutex) {
        /* Portal có thể lưu cấu hình TRƯỚC khi mqtt_client_init() chạy (MQTT
         * khởi động muộn, sau AP/portal). Không tự ý dựng client sớm: chỉ cập
         * nhật snapshot — mqtt_client_init() sau này sẽ dùng cấu hình mới nhất. */
        mqtt_copy_runtime_config(config);
        return;
    }
    mqtt_copy_runtime_config(config);
    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    mqtt_destroy_locked();
    xSemaphoreGive(s_client_mutex);
    mqtt_client_connect();
}

static void mqtt_publish(const char *topic, const char *payload, int qos, int retain)
{
    if (!s_publish_queue || !topic || !payload) return;

    MQTTMsg_t message = {0};
    strncpy(message.topic, topic, sizeof(message.topic) - 1);
    strncpy(message.payload, payload, sizeof(message.payload) - 1);
    message.qos = qos;
    message.retain = retain;

    /* Người gọi không bao giờ chờ trên socket, mutex MQTT hay outbox. */
    if (xQueueSend(s_publish_queue, &message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "MQTT TX queue full; dropping %s", topic);
        return;
    }
}

static void mqtt_publish_task_event(const char *type, int task, uint32_t seq, uint32_t timestamp)
{
    char topic[96], payload[256];
    snprintf(topic, sizeof(topic), MQTT_EVENT_TOPIC, s_config.callbox_id);
    snprintf(payload, sizeof(payload),
             "{\"type\":\"%s\",\"task\":%d,\"seq\":%lu,\"ts\":%lu}",
             type, task, (unsigned long)seq, (unsigned long)timestamp);
    mqtt_publish(topic, payload, MQTT_QoS, false);
}

void mqtt_publish_call(int task, uint32_t seq, uint32_t timestamp)
{
    mqtt_publish_task_event("call", task, seq, timestamp);
}

void mqtt_publish_cancel(int task, uint32_t seq, uint32_t timestamp)
{
    mqtt_publish_task_event("cancel", task, seq, timestamp);
}

void mqtt_publish_sync_request(uint32_t seq, uint32_t timestamp)
{
    char topic[96], payload[256];
    snprintf(topic, sizeof(topic), MQTT_EVENT_TOPIC, s_config.callbox_id);
    /* sync được phạm vi theo thiết bị: cố ý không chứa trường task. */
    snprintf(payload, sizeof(payload),
             "{\"type\":\"sync_request\",\"seq\":%lu,\"ts\":%lu,\"fw\":\"%s\"}",
             (unsigned long)seq, (unsigned long)timestamp, CALLBOX_FIRMWARE_VERSION);
    mqtt_publish(topic, payload, MQTT_QoS, false);
}

void mqtt_publish_status(void)
{
    char topic[96], payload[256];
    const uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);
    wifi_sta_status_t sta = {0};
    wifi_get_sta_status(&sta);
    /* Telemetry CHỈ ĐỌC từ status store — Mission Manager là chủ ghi duy nhất. */
    snprintf(topic, sizeof(topic), MQTT_STATUS_TOPIC, s_config.callbox_id);
    snprintf(payload, sizeof(payload),
             "{\"online\":true,\"comm\":\"%s\",\"task1\":\"%s\",\"task2\":\"%s\","
             "\"rssi\":%d,\"uptime\":%lu,\"time_synced\":%s,\"fw\":\"%s\",\"ts\":%lu}",
             mqtt_comm_state_name(status.CommState),
             mqtt_task_state_name(status.Mission[0]),
             mqtt_task_state_name(status.Mission[1]),
             (int)sta.rssi,
             (unsigned long)uptime_sec,
             time_sync_is_valid() ? "true" : "false",
             CALLBOX_FIRMWARE_VERSION,
             (unsigned long)time(NULL));
    mqtt_publish(topic, payload, MQTT_QoS, true);
}

void mqtt_event_handler_task(void *pvParameters)
{
    (void)pvParameters;
    /* Mission Manager tiêu thụ app_event_queue trực tiếp. */
    vTaskDelete(NULL);
}

void mqtt_communication_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_heartbeat_ms = 0;
    uint32_t last_start_ms = 0;
    ESP_LOGI(TAG, "MQTT supervisor started (ESP-MQTT auto reconnect enabled)");

    while (true) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (network_link_is_connected() && !s_client_started && now_ms - last_start_ms >= 5000U) {
            last_start_ms = now_ms;
            mqtt_client_connect();
        }
        if (s_mqtt_connected && now_ms - last_heartbeat_ms >= HEARTBEAT_INTERVAL_SEC * 1000UL) {
            mqtt_publish_status();
            last_heartbeat_ms = now_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

uint8_t mqtt_is_connected(void)
{
    return s_mqtt_connected ? 1U : 0U;
}
