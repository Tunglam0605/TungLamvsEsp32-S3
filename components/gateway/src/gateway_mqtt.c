#include "gateway_mqtt.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gateway_config.h"
#include "gateway_mqtt_json.h"
#include "gateway_network.h"
#include "gateway_topic.h"
#include "mqtt_client.h"
#include "warehouse_manager.h"

#define MQTT_POLL_MS 100U
#define MQTT_LEGACY_CLEAR_ACK_TIMEOUT_MS 1000U
#define MQTT_RECONFIGURE_WAIT_MS 4000U
#define MQTT_RECONFIGURE_REQUEST_BIT BIT0
#define MQTT_RECONFIGURE_DONE_BIT BIT1
#define VALID_UNIX_TIME 1700000000LL

static const char *TAG = "GW_MQTT";
static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_reconfigure_lock;
static EventGroupHandle_t s_control_events;
static volatile bool s_connected;
static volatile bool s_snapshot_requested;
static volatile int s_legacy_clear_msg_id = -1;
static volatile int s_last_published_msg_id = -1;
static volatile uint32_t s_published_event_count;
static volatile bool s_legacy_clear_acked;
static uint32_t s_sequence;
static char s_uri[128];
static char s_client_id[64];
static gateway_config_t s_active_config;
static gateway_topic_set_t s_topics;
static bool s_topic_error_reported;
static char s_user[48];
static char s_password[64];
static char s_payload[GATEWAY_MQTT_JSON_MAX];
static char s_bits_payload[(WAREHOUSE_POSITION_MAX * 2U) + 1U];

static int64_t timestamp_if_valid(void)
{
    const time_t now = time(NULL);
    return now >= (time_t)VALID_UNIX_TIME ? (int64_t)now : 0;
}

static bool tls_time_ready(void)
{
    return timestamp_if_valid() != 0;
}

static gateway_mqtt_json_context_t next_context(void)
{
    return (gateway_mqtt_json_context_t) {
        .company_id = s_active_config.company_id,
        .site_id = s_active_config.site_id,
        .warehouse_id = s_active_config.warehouse_id,
        .warehouse_name = s_active_config.warehouse_name,
        .sequence = ++s_sequence,
        .timestamp = timestamp_if_valid(),
    };
}

static int enqueue_payload(const char *topic, const char *payload, size_t length,
                           bool retain)
{
    if (s_client == NULL || !s_connected || topic == NULL || topic[0] == '\0' ||
        payload == NULL || length > INT_MAX) {
        return -1;
    }
    return esp_mqtt_client_enqueue(s_client, topic, payload, (int)length,
                                   1, retain, true);
}

static int enqueue_legacy_availability_clear(
    esp_mqtt_client_handle_t client, const gateway_config_t *config)
{
    char topic[GATEWAY_TOPIC_MAX];
    if (client == NULL ||
        gateway_topic_build_legacy_availability(config, topic,
                                                sizeof(topic)) != ESP_OK) {
        return -1;
    }
    /* MQTT retained-message deletion is a retained publish with an empty
     * payload.  This is migration cleanup only; no availability payload is
     * produced by the current two-topic contract. */
    return esp_mqtt_client_enqueue(client, topic, "", 0,
                                   1, true, true);
}

static void publish_snapshot(void)
{
    warehouse_snapshot_t snapshot;
    warehouse_manager_snapshot(&snapshot);
    const gateway_mqtt_json_context_t context = next_context();
    size_t json_length = 0;
    size_t bits_length = 0;
    const esp_err_t json_error = gateway_mqtt_json_snapshot(
        s_payload, sizeof(s_payload), &context, &snapshot, &json_length);
    const esp_err_t bits_error = gateway_mqtt_status_bits(
        s_bits_payload, sizeof(s_bits_payload), &snapshot, &bits_length);
    if (json_error != ESP_OK || bits_error != ESP_OK) {
        ESP_LOGE(TAG, "Cannot encode synchronized status: json=%s bits=%s",
                 esp_err_to_name(json_error), esp_err_to_name(bits_error));
        return;
    }
    const int json_id = enqueue_payload(s_topics.status_json, s_payload,
                                        json_length, true);
    const int bits_id = enqueue_payload(s_topics.status_bits, s_bits_payload,
                                        bits_length, true);
    if (json_id < 0 || bits_id < 0) {
        ESP_LOGW(TAG, "Cannot enqueue synchronized retained status: json=%d bits=%d",
                 json_id, bits_id);
    }
}

static void mqtt_event(void *argument, esp_event_base_t base, int32_t event_id,
                       void *event_data)
{
    (void)argument;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    if (event == NULL || event->client != s_client) return;
    if (event_id == MQTT_EVENT_CONNECTED) {
        s_connected = true;
        if (enqueue_legacy_availability_clear(event->client,
                                              &s_active_config) < 0) {
            ESP_LOGW(TAG, "Cannot enqueue legacy retained availability cleanup");
        }
        s_snapshot_requested = true;
        ESP_LOGI(TAG, "MQTT connected; synchronized status pending: %s | %s",
                 s_topics.status_json, s_topics.status_bits);
    } else if (event_id == MQTT_EVENT_PUBLISHED) {
        s_last_published_msg_id = event->msg_id;
        ++s_published_event_count;
        if (event->msg_id == s_legacy_clear_msg_id) s_legacy_clear_acked = true;
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected; CAN and warehouse remain active");
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "MQTT transport error");
    }
}

static void destroy_transport_locked(void)
{
    s_connected = false;
    s_legacy_clear_msg_id = -1;
    s_legacy_clear_acked = false;
    if (s_client != NULL) {
        (void)esp_mqtt_client_stop(s_client);
        (void)esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}

static void clear_old_legacy_availability_locked(void)
{
    if (s_client == NULL || !s_connected ||
        !gateway_identity_valid(&s_active_config)) {
        return;
    }

    s_legacy_clear_acked = false;
    s_legacy_clear_msg_id = -1;
    const uint32_t published_before = s_published_event_count;
    const int message_id = enqueue_legacy_availability_clear(
        s_client, &s_active_config);
    if (message_id < 0) {
        ESP_LOGW(TAG, "Cannot enqueue old legacy availability cleanup");
        return;
    }
    s_legacy_clear_msg_id = message_id;
    if (s_published_event_count != published_before &&
        s_last_published_msg_id == message_id) {
        s_legacy_clear_acked = true;
    }

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)MQTT_LEGACY_CLEAR_ACK_TIMEOUT_MS * 1000LL);
    while (!s_legacy_clear_acked && s_connected &&
           esp_timer_get_time() < deadline_us) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!s_legacy_clear_acked) {
        ESP_LOGW(TAG, "Timed out waiting for legacy availability cleanup PUBACK");
    }
    s_legacy_clear_msg_id = -1;
    s_legacy_clear_acked = false;
}

static bool prepare_topics_locked(const gateway_config_t *config)
{
    gateway_topic_set_t topics;
    const esp_err_t error = gateway_topic_build_set(config, &topics);
    if (error != ESP_OK) {
        memset(&s_topics, 0, sizeof(s_topics));
        memset(&s_active_config, 0, sizeof(s_active_config));
        if (!s_topic_error_reported) {
            ESP_LOGE(TAG, "MQTT disabled: invalid system identity/topic (%s)",
                     esp_err_to_name(error));
            s_topic_error_reported = true;
        }
        return false;
    }
    s_topic_error_reported = false;
    s_topics = topics;
    s_active_config = *config;
    return true;
}

static void connect_locked(void)
{
    gateway_config_t config;
    gateway_config_get(&config);
    if (s_client != NULL || !prepare_topics_locked(&config)) return;
    if (config.mqtt_broker[0] == '\0' || !gateway_network_production_available()) return;
    if (config.mqtt_transport == GATEWAY_MQTT_TLS && !tls_time_ready()) return;

    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u",
             config.mqtt_transport == GATEWAY_MQTT_TLS ? "mqtts" : "mqtt",
             config.mqtt_broker, config.mqtt_port);
    snprintf(s_client_id, sizeof(s_client_id), "AUBOT-GATEWAY-%s", config.gateway_id);
    strlcpy(s_user, config.mqtt_user, sizeof(s_user));
    strlcpy(s_password, config.mqtt_password, sizeof(s_password));

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = s_uri,
        .credentials.client_id = s_client_id,
        .credentials.username = s_user[0] ? s_user : NULL,
        .credentials.authentication.password = s_password[0] ? s_password : NULL,
        .session.keepalive = 30,
        .network.timeout_ms = 10000,
        .network.reconnect_timeout_ms = 5000,
    };
    if (config.mqtt_transport == GATEWAY_MQTT_TLS) {
        mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == NULL) return;
    (void)esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event, NULL);
    if (esp_mqtt_client_start(s_client) != ESP_OK) destroy_transport_locked();
}

static bool position_changed(const warehouse_position_t *previous,
                             const warehouse_position_t *current)
{
    return previous->config.enabled != current->config.enabled ||
           previous->config.position_id != current->config.position_id ||
           previous->config.group_id != current->config.group_id ||
           previous->config.laser_id != current->config.laser_id ||
           strcmp(previous->config.warehouse_code,
                  current->config.warehouse_code) != 0 ||
           strcmp(previous->config.warehouse_name,
                  current->config.warehouse_name) != 0 ||
           previous->state != current->state ||
           previous->sensor_online != current->sensor_online ||
           previous->status_valid != current->status_valid;
}

static bool refresh_display_identity_locked(const gateway_config_t *config)
{
    if (config == NULL || s_active_config.gateway_id[0] == '\0' ||
        strcmp(config->company_id, s_active_config.company_id) != 0 ||
        strcmp(config->site_id, s_active_config.site_id) != 0 ||
        strcmp(config->warehouse_id, s_active_config.warehouse_id) != 0 ||
        strcmp(config->gateway_id, s_active_config.gateway_id) != 0) {
        return false;
    }
    const bool changed = strcmp(config->warehouse_name,
                                s_active_config.warehouse_name) != 0;
    strlcpy(s_active_config.warehouse_name, config->warehouse_name,
            sizeof(s_active_config.warehouse_name));
    return changed;
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
        const EventBits_t control = xEventGroupClearBits(
            s_control_events, MQTT_RECONFIGURE_REQUEST_BIT);
        const bool reconfigure =
            (control & MQTT_RECONFIGURE_REQUEST_BIT) != 0U;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (reconfigure) {
            /* MQTT init/destroy needs considerably more stack than the HTTP
             * apply worker owns.  Keep the whole transport lifecycle on this
             * dedicated 12 KiB task to avoid corrupting the heap. */
            clear_old_legacy_availability_locked();
            destroy_transport_locked();
        }
        if (!gateway_network_production_available() && s_client != NULL) {
            destroy_transport_locked();
        }
        connect_locked();
        const bool display_identity_changed = refresh_display_identity_locked(&config);
        bool changed = display_identity_changed ||
                       (previous_valid && current.profile != previous.profile);
        if (previous_valid) {
            for (uint8_t i = 0; i < current.group_count; ++i) {
                const warehouse_position_t *old_position = &previous.positions[i];
                const warehouse_position_t *new_position = &current.positions[i];
                if (!position_changed(old_position, new_position)) continue;
                changed = true;
            }
        }
        if (s_connected && (s_snapshot_requested || changed || now_ms >= next_periodic_ms)) {
            publish_snapshot();
            s_snapshot_requested = false;
            next_periodic_ms = now_ms + config.publish_interval_ms;
        }
        xSemaphoreGive(s_lock);
        if (reconfigure) {
            xEventGroupSetBits(s_control_events, MQTT_RECONFIGURE_DONE_BIT);
        }

        previous = current;
        previous_valid = true;
        vTaskDelay(pdMS_TO_TICKS(MQTT_POLL_MS));
    }
}

esp_err_t gateway_mqtt_start(void)
{
    if (s_lock != NULL) return ESP_OK;
    s_sequence = 0;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    s_reconfigure_lock = xSemaphoreCreateMutex();
    s_control_events = xEventGroupCreate();
    if (s_reconfigure_lock == NULL || s_control_events == NULL) {
        if (s_control_events != NULL) vEventGroupDelete(s_control_events);
        if (s_reconfigure_lock != NULL) vSemaphoreDelete(s_reconfigure_lock);
        vSemaphoreDelete(s_lock);
        s_control_events = NULL;
        s_reconfigure_lock = NULL;
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(mqtt_task, "gw_mqtt", 12288, NULL, 5, NULL) != pdPASS) {
        vEventGroupDelete(s_control_events);
        vSemaphoreDelete(s_reconfigure_lock);
        vSemaphoreDelete(s_lock);
        s_control_events = NULL;
        s_reconfigure_lock = NULL;
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void gateway_mqtt_reconfigure(void)
{
    if (s_control_events == NULL || s_reconfigure_lock == NULL) return;
    if (xSemaphoreTake(s_reconfigure_lock,
                       pdMS_TO_TICKS(MQTT_RECONFIGURE_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Timed out serializing MQTT reconfiguration");
        return;
    }
    xEventGroupClearBits(s_control_events, MQTT_RECONFIGURE_DONE_BIT);
    xEventGroupSetBits(s_control_events, MQTT_RECONFIGURE_REQUEST_BIT);
    const EventBits_t result = xEventGroupWaitBits(
        s_control_events, MQTT_RECONFIGURE_DONE_BIT, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(MQTT_RECONFIGURE_WAIT_MS));
    if ((result & MQTT_RECONFIGURE_DONE_BIT) == 0U) {
        ESP_LOGW(TAG, "Timed out waiting for MQTT task reconfiguration");
    }
    xSemaphoreGive(s_reconfigure_lock);
}

void gateway_mqtt_request_snapshot(void)
{
    s_snapshot_requested = true;
}

bool gateway_mqtt_is_connected(void)
{
    return s_connected;
}

const char *gateway_mqtt_state_topic(void)
{
    return s_topics.status_json;
}

const char *gateway_mqtt_bits_topic(void)
{
    return s_topics.status_bits;
}
