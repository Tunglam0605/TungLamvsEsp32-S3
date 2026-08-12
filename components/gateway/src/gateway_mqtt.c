#include "gateway_mqtt.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gateway_config.h"
#include "gateway_mqtt_json.h"
#include "gateway_network.h"
#include "mqtt_client.h"
#include "warehouse_manager.h"

#define MQTT_POLL_MS 100U
#define VALID_UNIX_TIME 1700000000LL

static const char *TAG = "GW_MQTT";
static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_lock;
static volatile bool s_connected;
static volatile bool s_snapshot_requested;
static volatile bool s_ping_requested;
static uint32_t s_sequence;
static char s_boot_id[9];
static char s_uri[128];
static char s_client_id[40];
static char s_availability_topic[96];
static char s_status_topic[96];
static char s_event_topic[96];
static char s_command_topic[96];
static char s_lwt_payload[128];
static char s_user[48];
static char s_password[64];
static char s_payload[GATEWAY_MQTT_JSON_MAX];

static int64_t timestamp_if_valid(void)
{
    const time_t now = time(NULL);
    return now >= (time_t)VALID_UNIX_TIME ? (int64_t)now : 0;
}

static bool tls_time_ready(void)
{
    return timestamp_if_valid() != 0;
}

static gateway_mqtt_json_context_t next_context(const char *gateway_id)
{
    return (gateway_mqtt_json_context_t) {
        .gateway_id = gateway_id,
        .boot_id = s_boot_id,
        .sequence = ++s_sequence,
        .timestamp = timestamp_if_valid(),
    };
}

static bool enqueue_json(const char *topic, const char *payload, size_t length,
                         bool retain)
{
    if (s_client == NULL || !s_connected || length > INT_MAX) return false;
    return esp_mqtt_client_enqueue(s_client, topic, payload, (int)length,
                                   1, retain, true) >= 0;
}

static void publish_availability(esp_mqtt_client_handle_t client, bool online)
{
    gateway_config_t config;
    char payload[160];
    gateway_config_get(&config);
    size_t length = 0;
    if (gateway_mqtt_json_availability(payload, sizeof(payload),
                                       config.gateway_id, online,
                                       timestamp_if_valid(), &length) == ESP_OK) {
        (void)esp_mqtt_client_enqueue(client, s_availability_topic, payload,
                                      (int)length, 1, true, true);
    }
}

static void publish_snapshot(void)
{
    gateway_config_t config;
    warehouse_snapshot_t snapshot;
    gateway_config_get(&config);
    warehouse_manager_snapshot(&snapshot);
    const gateway_mqtt_json_context_t context = next_context(config.gateway_id);
    size_t length = 0;
    const esp_err_t error = gateway_mqtt_json_snapshot(
        s_payload, sizeof(s_payload), &context, &snapshot, &length);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Status JSON exceeds bounded buffer: %s", esp_err_to_name(error));
        return;
    }
    if (!enqueue_json(s_status_topic, s_payload, length, true)) {
        ESP_LOGW(TAG, "Cannot enqueue retained status snapshot");
    }
}

static void publish_warehouse_event(const warehouse_position_t *position,
                                    warehouse_state_t previous)
{
    gateway_config_t config;
    gateway_config_get(&config);
    const gateway_mqtt_json_context_t context = next_context(config.gateway_id);
    size_t length = 0;
    if (gateway_mqtt_json_warehouse_event(s_payload, sizeof(s_payload), &context,
                                          position, previous, &length) == ESP_OK) {
        (void)enqueue_json(s_event_topic, s_payload, length, false);
    }
}

static void publish_pong(void)
{
    gateway_config_t config;
    gateway_config_get(&config);
    const gateway_mqtt_json_context_t context = next_context(config.gateway_id);
    size_t length = 0;
    if (gateway_mqtt_json_ping(s_payload, sizeof(s_payload), &context, &length) == ESP_OK) {
        (void)enqueue_json(s_event_topic, s_payload, length, false);
    }
}

static bool topic_matches(const esp_mqtt_event_handle_t event, const char *topic)
{
    const size_t expected = strlen(topic);
    return event->topic != NULL && event->topic_len == (int)expected &&
           memcmp(event->topic, topic, expected) == 0;
}

static void handle_command(const esp_mqtt_event_handle_t event)
{
    if (!topic_matches(event, s_command_topic) || event->data == NULL ||
        event->current_data_offset != 0 || event->data_len != event->total_data_len) {
        return;
    }
    const gateway_mqtt_command_t command =
        gateway_mqtt_json_parse_command(event->data, (size_t)event->data_len);
    if (command == GATEWAY_MQTT_COMMAND_REQUEST_SNAPSHOT) s_snapshot_requested = true;
    else if (command == GATEWAY_MQTT_COMMAND_PING) s_ping_requested = true;
}

static void mqtt_event(void *argument, esp_event_base_t base, int32_t event_id,
                       void *event_data)
{
    (void)argument;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        s_connected = true;
        publish_availability(event->client, true);
        (void)esp_mqtt_client_subscribe(event->client, s_command_topic, 1);
        s_snapshot_requested = true;
        ESP_LOGI(TAG, "MQTT connected; production snapshot pending: %s", s_status_topic);
    } else if (event_id == MQTT_EVENT_DATA) {
        handle_command(event);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected; CAN and warehouse remain active");
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "MQTT transport error");
    }
}

static void destroy_locked(void)
{
    s_connected = false;
    if (s_client != NULL) {
        (void)esp_mqtt_client_stop(s_client);
        (void)esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}

static void connect_locked(void)
{
    gateway_config_t config;
    gateway_config_get(&config);
    if (s_client != NULL || config.mqtt_broker[0] == '\0' ||
        !gateway_network_production_available()) {
        return;
    }
    if (config.mqtt_transport == GATEWAY_MQTT_TLS && !tls_time_ready()) return;

    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u",
             config.mqtt_transport == GATEWAY_MQTT_TLS ? "mqtts" : "mqtt",
             config.mqtt_broker, config.mqtt_port);
    snprintf(s_client_id, sizeof(s_client_id), "AUBOT-GATEWAY-%s", config.gateway_id);
    snprintf(s_availability_topic, sizeof(s_availability_topic),
             "gateway/%s/availability", config.gateway_id);
    snprintf(s_status_topic, sizeof(s_status_topic), "gateway/%s/status", config.gateway_id);
    snprintf(s_event_topic, sizeof(s_event_topic), "gateway/%s/event", config.gateway_id);
    snprintf(s_command_topic, sizeof(s_command_topic), "gateway/%s/cmd", config.gateway_id);
    strlcpy(s_user, config.mqtt_user, sizeof(s_user));
    strlcpy(s_password, config.mqtt_password, sizeof(s_password));
    size_t lwt_length = 0;
    if (gateway_mqtt_json_availability(s_lwt_payload, sizeof(s_lwt_payload),
                                       config.gateway_id, false, 0, &lwt_length) != ESP_OK) {
        return;
    }

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = s_uri,
        .credentials.client_id = s_client_id,
        .credentials.username = s_user[0] ? s_user : NULL,
        .credentials.authentication.password = s_password[0] ? s_password : NULL,
        .session.keepalive = 30,
        .session.last_will.topic = s_availability_topic,
        .session.last_will.msg = s_lwt_payload,
        .session.last_will.msg_len = (int)lwt_length,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
        .network.timeout_ms = 10000,
        .network.reconnect_timeout_ms = 5000,
    };
    if (config.mqtt_transport == GATEWAY_MQTT_TLS) {
        mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == NULL) return;
    (void)esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event, NULL);
    if (esp_mqtt_client_start(s_client) != ESP_OK) destroy_locked();
}

static bool position_changed(const warehouse_position_t *previous,
                             const warehouse_position_t *current)
{
    return previous->config.enabled != current->config.enabled ||
           previous->config.laser_id != current->config.laser_id ||
           strcmp(previous->config.warehouse_code,
                  current->config.warehouse_code) != 0 ||
           previous->state != current->state ||
           previous->sensor_online != current->sensor_online;
}

static void mqtt_task(void *argument)
{
    (void)argument;
    warehouse_snapshot_t previous = {0};
    bool previous_valid = false;
    int64_t next_periodic_ms = 0;

    for (;;) {
        gateway_config_t config;
        warehouse_snapshot_t current;
        gateway_config_get(&config);
        warehouse_manager_snapshot(&current);
        const int64_t now_ms = esp_timer_get_time() / 1000LL;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!gateway_network_production_available() && s_client != NULL) {
            destroy_locked();
        }
        connect_locked();
        bool changed = previous_valid && current.profile != previous.profile;
        if (previous_valid) {
            for (uint8_t i = 0; i < current.group_count; ++i) {
                const warehouse_position_t *old_position = &previous.positions[i];
                const warehouse_position_t *new_position = &current.positions[i];
                if (!position_changed(old_position, new_position)) continue;
                changed = true;
                if (s_connected && old_position->config.enabled &&
                    new_position->config.enabled &&
                    old_position->state != new_position->state) {
                    publish_warehouse_event(new_position, old_position->state);
                }
            }
        }
        if (s_connected && (s_snapshot_requested || changed || now_ms >= next_periodic_ms)) {
            publish_snapshot();
            s_snapshot_requested = false;
            next_periodic_ms = now_ms + config.publish_interval_ms;
        }
        if (s_connected && s_ping_requested) {
            publish_pong();
            s_ping_requested = false;
        }
        xSemaphoreGive(s_lock);

        previous = current;
        previous_valid = true;
        vTaskDelay(pdMS_TO_TICKS(MQTT_POLL_MS));
    }
}

esp_err_t gateway_mqtt_start(void)
{
    if (s_lock != NULL) return ESP_OK;
    snprintf(s_boot_id, sizeof(s_boot_id), "%08" PRIX32, esp_random());
    s_sequence = 0;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreate(mqtt_task, "gw_mqtt", 12288, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

void gateway_mqtt_reconfigure(void)
{
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    destroy_locked();
    connect_locked();
    xSemaphoreGive(s_lock);
}

bool gateway_mqtt_is_connected(void)
{
    return s_connected;
}

const char *gateway_mqtt_state_topic(void)
{
    return s_status_topic;
}
