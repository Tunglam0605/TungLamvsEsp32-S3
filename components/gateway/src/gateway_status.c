#include "gateway_status.h"

#include "bsp_buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "platform_wifi.h"

typedef enum {
    BEEP_NETWORK_UP,
    BEEP_NETWORK_DOWN,
    BEEP_AP_ON,
    BEEP_AP_OFF,
    BEEP_MQTT_UP,
} beep_pattern_t;

static const char *TAG = "GW_STATUS";
static QueueHandle_t s_beep_queue;

static void tone(uint32_t hz, uint32_t duration_ms)
{
    (void)bsp_buzzer_set(hz, 45);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    (void)bsp_buzzer_off();
}

static void gap(uint32_t duration_ms) { vTaskDelay(pdMS_TO_TICKS(duration_ms)); }

static void short_beep(void) { tone(2400, 120); }
static void long_beep(void) { tone(1600, 650); }

static void buzzer_task(void *arg)
{
    (void)arg;
    beep_pattern_t pattern;
    for (;;) {
        if (xQueueReceive(s_beep_queue, &pattern, portMAX_DELAY) != pdTRUE) continue;
        switch (pattern) {
        case BEEP_NETWORK_UP:
            short_beep(); gap(100); short_beep();
            break;
        case BEEP_NETWORK_DOWN:
            long_beep();
            break;
        case BEEP_AP_ON:
            short_beep(); gap(90); short_beep(); gap(90); short_beep();
            break;
        case BEEP_AP_OFF:
            long_beep(); gap(180); long_beep();
            break;
        case BEEP_MQTT_UP:
            short_beep(); gap(120); long_beep(); gap(160); long_beep();
            break;
        }
        gap(180);
    }
}

static void queue_beep(beep_pattern_t pattern)
{
    if (xQueueSend(s_beep_queue, &pattern, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Bo qua am bao vi hang doi dang day");
    }
}

static void status_task(void *arg)
{
    (void)arg;
    bool initialized = false;
    bool previous_network = false, previous_mqtt = false, previous_ap = false;
    for (;;) {
        const bool mqtt = gateway_mqtt_is_connected();
        const bool network = gateway_network_is_connected();

        const bool ap = platform_wifi_ap_is_active();

        if (!initialized) {
            previous_network = network;
            previous_mqtt = mqtt;
            previous_ap = false; /* Bao AP dang phat o lan boot dau. */
            initialized = true;
        }
        if (network != previous_network) {
            queue_beep(network ? BEEP_NETWORK_UP : BEEP_NETWORK_DOWN);
            ESP_LOGI(TAG, "Mang uplink: %s", network ? "DA KET NOI" : "MAT KET NOI");
        }
        if (mqtt && !previous_mqtt) {
            queue_beep(BEEP_MQTT_UP);
            ESP_LOGI(TAG, "MQTT da ket noi/ket noi lai");
        }
        if (ap != previous_ap) {
            queue_beep(ap ? BEEP_AP_ON : BEEP_AP_OFF);
            ESP_LOGI(TAG, "AP cau hinh: %s", ap ? "BAT" : "TAT");
        }
        previous_network = network;
        previous_mqtt = mqtt;
        previous_ap = ap;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t gateway_status_start(void)
{
    s_beep_queue = xQueueCreate(10, sizeof(beep_pattern_t));
    if (!s_beep_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(buzzer_task, "gw_buzzer", 3072, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(status_task, "gw_status", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
