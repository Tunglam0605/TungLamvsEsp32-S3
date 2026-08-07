/**
 * @file    mqtt_client.c
 * @brief   MQTT transport adapter for Callbox.
 *
 * The application protocol remains callbox/<id>/{cmd,event,status}.  This
 * module delegates MQTT framing, keep-alive, reconnect and TLS to ESP-MQTT so
 * the same firmware works with an internal TCP broker or a public TLS broker.
 */
#include "callbox_mqtt.h"

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "app_event_queue.h"
#include "protocol_types.h"
#include "state_machine.h"
#include "wifi_init.h"
#include "time_sync.h"

#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "MQTT_CLIENT";

static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_client_mutex;
static volatile bool s_mqtt_connected;
static volatile bool s_client_started;
static QueueHandle_t s_publish_queue;

static void mqtt_publish_worker(void *pvParameters)
{
    (void)pvParameters;
    MQTTMsg_t message;

    for (;;) {
        if (xQueueReceive(s_publish_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Only this low-priority worker may touch the ESP-MQTT TX API.  If
         * the broker/outbox blocks, button/state tasks remain schedulable. */
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

/* ESP-MQTT keeps these pointers for the life of the client.  Copy every
 * user-facing value before opening the connection so save/reconfigure cannot
 * leave the transport referencing a modified Config_t field. */
static char s_uri[160];
static char s_client_id[64];
static char s_username[sizeof(g_config.mqtt_user)];
static char s_password[sizeof(g_config.mqtt_pass)];
static char s_status_topic[96];

static const char *mqtt_transport_name(MqttTransport_t transport)
{
    return transport == MQTT_TRANSPORT_TLS ? "TLS" : "TCP";
}

/* Internal enums intentionally use upper-case names; MQTT follows the WCS
 * wire contract, which uses lower-case state strings. */
static const char *mqtt_task_state_name(int task)
{
    switch (get_task_state(task)) {
    case TASK_QUEUED: return "queued";
    case TASK_ASSIGNED: return "assigned";
    case TASK_LOCKED: return "locked";
    case TASK_COMPLETED: return "completed";
    default: return "idle";
    }
}

static void mqtt_handle_command(const char *topic, const char *payload)
{
    char expected_topic[96];
    snprintf(expected_topic, sizeof(expected_topic), MQTT_CMD_TOPIC, g_config.callbox_id);
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
        snprintf(topic, sizeof(topic), MQTT_CMD_TOPIC, g_config.callbox_id);
        const int message_id = esp_mqtt_client_subscribe(event->client, topic, MQTT_QoS);
        const app_event_t app_event = { .type = APP_EVENT_MQTT_CONNECTED };
        (void)app_event_send(&app_event, 0);
        ESP_LOGI(TAG, "Connected via %s; subscribe %s (id=%d)",
                 mqtt_transport_name(g_config.mqtt_transport), topic, message_id);
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
        /* Callbox commands are intentionally small.  Do not parse fragmented
         * payloads as a complete command; oversized payloads are ignored. */
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

void mqtt_client_init(void)
{
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
    snprintf(topic, sizeof(topic), MQTT_CMD_TOPIC, g_config.callbox_id);
    (void)esp_mqtt_client_subscribe(s_client, topic, MQTT_QoS);
}

void mqtt_client_connect(void)
{
    if (!s_client_mutex || !network_is_connected() ||
        !g_config.mqtt_broker[0] || g_config.mqtt_port == 0) {
        return;
    }
    if (g_config.mqtt_transport == MQTT_TRANSPORT_TLS && !time_sync_is_valid()) {
        ESP_LOGI(TAG, "TLS selected: waiting for SNTP time synchronization");
        return;
    }

    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    if (s_client_started) {
        xSemaphoreGive(s_client_mutex);
        return;
    }

    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u",
             g_config.mqtt_transport == MQTT_TRANSPORT_TLS ? "mqtts" : "mqtt",
             g_config.mqtt_broker, (unsigned)g_config.mqtt_port);
    snprintf(s_client_id, sizeof(s_client_id), CALLBOX_DEVICE_NAME_PREFIX "%s",
             g_config.callbox_id);
    snprintf(s_status_topic, sizeof(s_status_topic), MQTT_STATUS_TOPIC, g_config.callbox_id);
    strncpy(s_username, g_config.mqtt_user, sizeof(s_username) - 1);
    s_username[sizeof(s_username) - 1] = '\0';
    strncpy(s_password, g_config.mqtt_pass, sizeof(s_password) - 1);
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
    if (g_config.mqtt_transport == MQTT_TRANSPORT_TLS) {
        /* Public CA validation is required: never disable hostname/CN checks. */
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
        ESP_LOGI(TAG, "Connecting %s to %s as %s", mqtt_transport_name(g_config.mqtt_transport),
                 s_uri, s_client_id);
    }
    xSemaphoreGive(s_client_mutex);
}

void mqtt_client_reconfigure(void)
{
    if (!s_client_mutex) return;
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

    /* The caller never waits on the socket, MQTT mutex, or outbox. */
    if (xQueueSend(s_publish_queue, &message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "MQTT TX queue full; dropping %s", topic);
        return;
    }
}

static void mqtt_publish_task_event(const char *type, int task, uint32_t seq, uint32_t timestamp)
{
    char topic[96], payload[256];
    snprintf(topic, sizeof(topic), MQTT_EVENT_TOPIC, g_config.callbox_id);
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
    snprintf(topic, sizeof(topic), MQTT_EVENT_TOPIC, g_config.callbox_id);
    /* sync is device-scoped: intentionally contains no task field. */
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
    snprintf(topic, sizeof(topic), MQTT_STATUS_TOPIC, g_config.callbox_id);
    snprintf(payload, sizeof(payload),
             "{\"online\":true,\"comm\":\"%s\",\"task1\":\"%s\",\"task2\":\"%s\","
             "\"rssi\":%d,\"uptime\":%lu,\"time_synced\":%s,\"fw\":\"%s\",\"ts\":%lu}",
             get_comm_state_str(), mqtt_task_state_name(1), mqtt_task_state_name(2), (int)sta.rssi,
             (unsigned long)uptime_sec,
             time_sync_is_valid() ? "true" : "false",
             CALLBOX_FIRMWARE_VERSION,
             (unsigned long)time(NULL));
    mqtt_publish(topic, payload, MQTT_QoS, true);
}

void mqtt_event_handler_task(void *pvParameters)
{
    (void)pvParameters;
    /* Mission Manager consumes app_event_queue directly. */
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
        if (network_is_connected() && !s_client_started && now_ms - last_start_ms >= 5000U) {
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
