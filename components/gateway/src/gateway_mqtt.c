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
#define MQTT_RETAINED_CLEAR_ACK_TIMEOUT_MS 1000U
#define MQTT_RETAINED_CLEAR_MAX 3U
#define MQTT_PUBLISHED_HISTORY_SIZE 8U
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
static volatile bool s_pending_cleanup_requested;
static portMUX_TYPE s_publish_event_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_published_event_count;
static int s_published_msg_history[MQTT_PUBLISHED_HISTORY_SIZE];
static int s_retained_clear_msg_ids[MQTT_RETAINED_CLEAR_MAX];
static bool s_retained_clear_acked[MQTT_RETAINED_CLEAR_MAX];
static size_t s_retained_clear_count;
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

static int enqueue_retained_clear(esp_mqtt_client_handle_t client,
                                  const char *topic)
{
    if (client == NULL || topic == NULL || topic[0] == '\0') return -1;
    return esp_mqtt_client_enqueue(client, topic, "", 0,
                                   1, true, true);
}

static uint32_t published_event_count(void)
{
    uint32_t count;
    portENTER_CRITICAL(&s_publish_event_mux);
    count = s_published_event_count;
    portEXIT_CRITICAL(&s_publish_event_mux);
    return count;
}

static bool register_retained_clear(int message_id, uint32_t published_before)
{
    if (message_id < 0) return false;

    bool registered = false;
    portENTER_CRITICAL(&s_publish_event_mux);
    if (s_retained_clear_count < MQTT_RETAINED_CLEAR_MAX) {
        const size_t slot = s_retained_clear_count++;
        s_retained_clear_msg_ids[slot] = message_id;
        s_retained_clear_acked[slot] = false;

        const uint32_t published_now = s_published_event_count;
        uint32_t event_count = published_now - published_before;
        if (event_count > MQTT_PUBLISHED_HISTORY_SIZE) {
            event_count = MQTT_PUBLISHED_HISTORY_SIZE;
        }
        const uint32_t first = published_now - event_count;
        for (uint32_t offset = 0; offset < event_count; ++offset) {
            const uint32_t sequence = first + offset;
            if (s_published_msg_history[
                    sequence % MQTT_PUBLISHED_HISTORY_SIZE] == message_id) {
                s_retained_clear_acked[slot] = true;
                break;
            }
        }
        registered = true;
    }
    portEXIT_CRITICAL(&s_publish_event_mux);
    return registered;
}

static size_t retained_clear_ack_count(size_t *total)
{
    size_t acknowledged = 0;
    portENTER_CRITICAL(&s_publish_event_mux);
    const size_t count = s_retained_clear_count;
    for (size_t i = 0; i < count; ++i) {
        if (s_retained_clear_acked[i]) ++acknowledged;
    }
    portEXIT_CRITICAL(&s_publish_event_mux);
    if (total != NULL) *total = count;
    return acknowledged;
}

static bool retained_clear_message_acked(int message_id)
{
    bool acknowledged = false;
    portENTER_CRITICAL(&s_publish_event_mux);
    for (size_t i = 0; i < s_retained_clear_count; ++i) {
        if (s_retained_clear_msg_ids[i] == message_id) {
            acknowledged = s_retained_clear_acked[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_publish_event_mux);
    return acknowledged;
}

static void reset_retained_clear_tracking(void)
{
    portENTER_CRITICAL(&s_publish_event_mux);
    s_retained_clear_count = 0;
    for (size_t i = 0; i < MQTT_RETAINED_CLEAR_MAX; ++i) {
        s_retained_clear_msg_ids[i] = -1;
        s_retained_clear_acked[i] = false;
    }
    portEXIT_CRITICAL(&s_publish_event_mux);
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
        /* A retained namespace migration may have survived a reboot.  Attempt
         * it once per connection (not every 100 ms task poll); the persistent
         * marker remains for the next reconnect if either status PUBACK is
         * missing.  The bounded wait runs on the MQTT owner task, outside this
         * event callback. */
        gateway_config_t pending_identity;
        s_pending_cleanup_requested =
            gateway_config_get_pending_mqtt_identity(&pending_identity);
        s_snapshot_requested = true;
        ESP_LOGI(TAG, "MQTT connected; synchronized status pending: %s | %s",
                 s_topics.status_json, s_topics.status_bits);
    } else if (event_id == MQTT_EVENT_PUBLISHED) {
        portENTER_CRITICAL(&s_publish_event_mux);
        const uint32_t sequence = s_published_event_count++;
        s_published_msg_history[
            sequence % MQTT_PUBLISHED_HISTORY_SIZE] = event->msg_id;
        for (size_t i = 0; i < s_retained_clear_count; ++i) {
            if (s_retained_clear_msg_ids[i] == event->msg_id) {
                s_retained_clear_acked[i] = true;
            }
        }
        portEXIT_CRITICAL(&s_publish_event_mux);
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
    reset_retained_clear_tracking();
    if (s_client != NULL) {
        (void)esp_mqtt_client_stop(s_client);
        (void)esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}

static bool status_topic_paths_differ(const gateway_config_t *old_config,
                                      const gateway_config_t *next_config,
                                      gateway_topic_set_t *old_topics)
{
    gateway_topic_set_t previous_topics;
    gateway_topic_set_t next_topics;
    if (old_config == NULL || next_config == NULL ||
        gateway_topic_build_set(old_config, &previous_topics) != ESP_OK ||
        gateway_topic_build_set(next_config, &next_topics) != ESP_OK) {
        /* Do not erase a valid retained snapshot when the replacement
         * identity cannot produce valid topics. */
        return false;
    }
    if (old_topics != NULL) *old_topics = previous_topics;
    return strcmp(next_topics.status_json, previous_topics.status_json) != 0 ||
           strcmp(next_topics.status_bits, previous_topics.status_bits) != 0;
}

static void clear_old_retained_topics_locked(
    const gateway_config_t *next_config)
{
    if (s_client == NULL || !s_connected ||
        !gateway_identity_valid(&s_active_config)) {
        return;
    }

    gateway_config_t old_identity;
    bool pending_identity =
        gateway_config_get_pending_mqtt_identity(&old_identity);
    gateway_topic_set_t old_topics = {0};
    bool clear_old_status = pending_identity &&
        status_topic_paths_differ(&old_identity, next_config, &old_topics);

    if (!pending_identity &&
        status_topic_paths_differ(&s_active_config, next_config, &old_topics)) {
        /* Defensive same-boot fallback.  Normally gateway_config_save()
         * persists s_active_config as the pending identity before requesting
         * this reconfiguration. */
        old_identity = s_active_config;
        clear_old_status = true;
    }

    reset_retained_clear_tracking();
    const gateway_config_t *legacy_identity =
        clear_old_status ? &old_identity : &s_active_config;

    uint32_t published_before = published_event_count();
    const int availability_message_id = enqueue_legacy_availability_clear(
        s_client, legacy_identity);
    if (!register_retained_clear(availability_message_id, published_before)) {
        ESP_LOGW(TAG, "Cannot enqueue old legacy availability cleanup");
    }

    int json_message_id = -1;
    int bits_message_id = -1;
    if (clear_old_status) {
        published_before = published_event_count();
        json_message_id = enqueue_retained_clear(s_client,
                                                  old_topics.status_json);
        if (!register_retained_clear(json_message_id, published_before)) {
            ESP_LOGW(TAG, "Cannot enqueue old retained JSON status cleanup");
        }

        published_before = published_event_count();
        bits_message_id = enqueue_retained_clear(s_client,
                                                  old_topics.status_bits);
        if (!register_retained_clear(bits_message_id, published_before)) {
            ESP_LOGW(TAG, "Cannot enqueue old retained bits status cleanup");
        }
        ESP_LOGI(TAG, "Clearing retained status for old identity: %s | %s",
                 old_topics.status_json, old_topics.status_bits);
    }

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)MQTT_RETAINED_CLEAR_ACK_TIMEOUT_MS * 1000LL);
    size_t total = 0;
    size_t acknowledged = retained_clear_ack_count(&total);
    while (acknowledged < total && s_connected &&
           esp_timer_get_time() < deadline_us) {
        vTaskDelay(pdMS_TO_TICKS(10));
        acknowledged = retained_clear_ack_count(&total);
    }
    if (acknowledged < total) {
        ESP_LOGW(TAG, "Timed out waiting for retained cleanup PUBACKs (%u/%u)",
                 (unsigned)acknowledged, (unsigned)total);
    }
    const bool availability_cleared = availability_message_id >= 0 &&
        retained_clear_message_acked(availability_message_id);
    const bool status_cleared = !clear_old_status ||
        (json_message_id >= 0 && bits_message_id >= 0 &&
         retained_clear_message_acked(json_message_id) &&
         retained_clear_message_acked(bits_message_id));
    if (pending_identity && availability_cleared && status_cleared) {
        const esp_err_t error = gateway_config_clear_pending_mqtt_identity();
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "Cannot persist retained status cleanup: %s",
                     esp_err_to_name(error));
        }
    }
    reset_retained_clear_tracking();
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
            clear_old_retained_topics_locked(&config);
            destroy_transport_locked();
        }
        if (!gateway_network_production_available() && s_client != NULL) {
            destroy_transport_locked();
        }
        connect_locked();
        if (s_connected && s_pending_cleanup_requested) {
            s_pending_cleanup_requested = false;
            clear_old_retained_topics_locked(&config);
        }
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
