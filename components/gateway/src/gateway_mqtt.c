#include "gateway_mqtt.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gateway_config.h"
#include "gateway_network.h"
#include "mqtt_client.h"
#include "warehouse_manager.h"

static const char *TAG = "GW_MQTT";
static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_lock;
static volatile bool s_connected;
static uint32_t s_sequence;
static char s_uri[128], s_client_id[40], s_status_topic[96], s_state_topic[96];
static char s_user[48], s_password[64];

static bool tls_time_ready(void) { time_t now = 0; time(&now); return now > 1700000000; }

static void publish_maps(void)
{
    warehouse_slot_status_t slots[64];
    size_t n = warehouse_manager_get_slots(slots, 64, NULL);
    for (size_t i = 0; i < n; ++i) {
        char topic[112], json[220];
        gateway_config_t cfg; gateway_config_get(&cfg);
        snprintf(topic, sizeof(topic), "gateway/%s/warehouse/map/%u", cfg.gateway_id,
                 slots[i].mapping.slot_index);
        int len = snprintf(json, sizeof(json),
            "{\"slot_index\":%u,\"laser_id\":%u,\"cluster_id\":%u,\"slot_code\":\"%s\",\"slot_name\":\"%s\"}",
            slots[i].mapping.slot_index, slots[i].mapping.laser_id,
            slots[i].mapping.cluster_id, slots[i].mapping.slot_code,
            slots[i].mapping.slot_name);
        if (len > 0 && len < sizeof(json)) (void)esp_mqtt_client_enqueue(s_client, topic, json, len, 1, true, true);
    }
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; esp_mqtt_event_handle_t e = data;
    if (id == MQTT_EVENT_CONNECTED) {
        s_connected = true;
        (void)esp_mqtt_client_enqueue(e->client, s_status_topic, "{\"online\":true}", 15, 1, true, true);
        publish_maps();
        ESP_LOGI(TAG, "Da ket noi MQTT; topic trang thai nhi phan: %s", s_state_topic);
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "Mat ket noi MQTT, ESP-MQTT se tu ket noi lai");
    } else if (id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "Loi transport MQTT");
    }
}

static void destroy_locked(void)
{
    s_connected = false;
    if (s_client) { (void)esp_mqtt_client_stop(s_client); (void)esp_mqtt_client_destroy(s_client); s_client = NULL; }
}

static void connect_locked(void)
{
    gateway_config_t c; gateway_config_get(&c);
    if (s_client || !c.mqtt_broker[0] || !gateway_network_is_connected()) return;
    if (c.mqtt_transport == GATEWAY_MQTT_TLS && !tls_time_ready()) return;
    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u", c.mqtt_transport == GATEWAY_MQTT_TLS ? "mqtts" : "mqtt", c.mqtt_broker, c.mqtt_port);
    snprintf(s_client_id, sizeof(s_client_id), "AUBOT-GATEWAY-%s", c.gateway_id);
    snprintf(s_status_topic, sizeof(s_status_topic), "gateway/%s/status", c.gateway_id);
    snprintf(s_state_topic, sizeof(s_state_topic), "gateway/%s/warehouse/state", c.gateway_id);
    snprintf(s_user, sizeof(s_user), "%s", c.mqtt_user); snprintf(s_password, sizeof(s_password), "%s", c.mqtt_password);
    esp_mqtt_client_config_t mc = {
        .broker.address.uri=s_uri,.credentials.client_id=s_client_id,
        .credentials.username=s_user[0]?s_user:NULL,
        .credentials.authentication.password=s_password[0]?s_password:NULL,
        .session.keepalive=30,.session.last_will.topic=s_status_topic,
        .session.last_will.msg="{\"online\":false}",.session.last_will.qos=1,
        .session.last_will.retain=true,.network.timeout_ms=10000,.network.reconnect_timeout_ms=5000};
    if (c.mqtt_transport == GATEWAY_MQTT_TLS) mc.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    s_client = esp_mqtt_client_init(&mc);
    if (!s_client) return;
    (void)esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event, NULL);
    if (esp_mqtt_client_start(s_client) != ESP_OK) destroy_locked();
}

static void publish_state(void)
{
    /* Frame cố định 32 byte, little-endian:
     * A5 01 01 40 | sequence u32 | assigned[8] | online[8] | occupied[8]. */
    uint8_t frame[32] = {0xA5, 0x01, 0x01, 64};
    uint32_t seq = ++s_sequence;
    frame[4]=seq; frame[5]=seq>>8; frame[6]=seq>>16; frame[7]=seq>>24;
    warehouse_slot_status_t slots[64]; size_t n = warehouse_manager_get_slots(slots, 64, NULL);
    for (size_t i = 0; i < n; ++i) {
        uint8_t index = slots[i].mapping.slot_index - 1U, mask = 1U << (index & 7U), byte = index >> 3U;
        frame[8 + byte] |= mask;
        if (slots[i].sensor_online) frame[16 + byte] |= mask;
        if (slots[i].sensor_online && slots[i].state == WAREHOUSE_SLOT_OCCUPIED) frame[24 + byte] |= mask;
    }
    (void)esp_mqtt_client_enqueue(s_client, s_state_topic, (const char *)frame, sizeof(frame), 1, false, true);
}

static void mqtt_task(void *arg)
{
    (void)arg; uint32_t map_tick = 0;
    for (;;) {
        gateway_config_t c; gateway_config_get(&c);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        connect_locked();
        if (s_connected && s_client) { publish_state(); if (++map_tick >= 30) { publish_maps(); map_tick=0; } }
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(c.publish_interval_ms));
    }
}

esp_err_t gateway_mqtt_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    /* Frame publisher cung chup mang 64 slot, can du stack cho JSON mapping. */
    return xTaskCreate(mqtt_task, "gw_mqtt", 10240, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void gateway_mqtt_reconfigure(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    destroy_locked();
    connect_locked();
    xSemaphoreGive(s_lock);
}
bool gateway_mqtt_is_connected(void) { return s_connected; }
const char *gateway_mqtt_state_topic(void) { return s_state_topic; }
