#include "gateway_output.h"

#include <string.h>
#include "bsp_buzzer.h"
#include "bsp_do.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gateway_io.h"

#define OUTPUT_QUEUE_DEPTH 12U
#define OUTPUT_TICK_MS 20U
#define TOWER_BLINK_MS 500U
#define ONBOARD_BUZZER_HZ 2000U
#define ONBOARD_BUZZER_DUTY 50U

typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
} buzzer_step_t;

typedef struct {
    const buzzer_step_t *steps;
    uint8_t count;
} buzzer_pattern_t;

#define B(on, off) { (on), (off) }
/* Mã nhịp hiện trường: S = ngắn 100 ms, L = dài 450 ms.
 * Mỗi sự kiện có một mã riêng và kết thúc bằng 400 ms im lặng để hai sự kiện
 * liên tiếp không dính thành một chuỗi khó nhận biết. */
static const buzzer_step_t P_AP_ON[] = {B(100,100),B(100,100),B(100,400)};       /* S S S */
static const buzzer_step_t P_AP_OFF[] = {B(450,120),B(450,400)};                  /* L L */
static const buzzer_step_t P_NET_UP[] = {B(100,100),B(100,400)};                  /* S S */
static const buzzer_step_t P_NET_DOWN[] = {B(450,400)};                           /* L */
static const buzzer_step_t P_MQTT_UP[] = {B(100,120),B(450,120),B(450,400)};      /* S L L */
static const buzzer_step_t P_MQTT_DOWN[] = {B(450,120),B(100,120),B(100,400)};    /* L S S */
static const buzzer_step_t P_CAN_OFF[] = {B(450,120),B(100,120),B(450,400)};      /* L S L */
static const buzzer_step_t P_CAN_RECOVERED[] = {B(100,120),B(450,120),B(100,400)}; /* S L S */
static const buzzer_step_t P_LASER_OFF[] = {B(450,100),B(100,100),B(100,100),B(100,400)}; /* L S S S */
static const buzzer_step_t P_LASER_RECOVERED[] = {B(100,100),B(100,100),B(100,120),B(450,400)}; /* S S S L */
static const buzzer_step_t P_MISMATCH[] = {B(100,100),B(450,100),B(100,100),B(450,400)}; /* S L S L */

#define PATTERN(steps) { (steps), (uint8_t)(sizeof(steps) / sizeof((steps)[0])) }
static const buzzer_pattern_t PATTERNS[GATEWAY_DIAG_EVENT_COUNT] = {
    [GATEWAY_DIAG_AP_ON] = PATTERN(P_AP_ON),
    [GATEWAY_DIAG_AP_OFF] = PATTERN(P_AP_OFF),
    [GATEWAY_DIAG_NETWORK_UP] = PATTERN(P_NET_UP),
    [GATEWAY_DIAG_NETWORK_DOWN] = PATTERN(P_NET_DOWN),
    [GATEWAY_DIAG_MQTT_UP] = PATTERN(P_MQTT_UP),
    [GATEWAY_DIAG_MQTT_DOWN] = PATTERN(P_MQTT_DOWN),
    [GATEWAY_DIAG_CAN_BUS_OFF] = PATTERN(P_CAN_OFF),
    [GATEWAY_DIAG_CAN_RECOVERED] = PATTERN(P_CAN_RECOVERED),
    [GATEWAY_DIAG_LASER_OFFLINE] = PATTERN(P_LASER_OFF),
    [GATEWAY_DIAG_LASER_RECOVERED] = PATTERN(P_LASER_RECOVERED),
    [GATEWAY_DIAG_CONFIG_MISMATCH] = PATTERN(P_MISMATCH),
};

static const char *event_name(gateway_diagnostic_event_t event)
{
    static const char *const names[GATEWAY_DIAG_EVENT_COUNT] = {
        [GATEWAY_DIAG_AP_ON] = "AP_ON SSS",
        [GATEWAY_DIAG_AP_OFF] = "AP_OFF LL",
        [GATEWAY_DIAG_NETWORK_UP] = "NETWORK_UP SS",
        [GATEWAY_DIAG_NETWORK_DOWN] = "NETWORK_DOWN L",
        [GATEWAY_DIAG_MQTT_UP] = "MQTT_UP SLL",
        [GATEWAY_DIAG_MQTT_DOWN] = "MQTT_DOWN LSS",
        [GATEWAY_DIAG_CAN_BUS_OFF] = "CAN_BUS_OFF LSL",
        [GATEWAY_DIAG_CAN_RECOVERED] = "CAN_RECOVERED SLS",
        [GATEWAY_DIAG_LASER_OFFLINE] = "LASER_OFFLINE LSSS",
        [GATEWAY_DIAG_LASER_RECOVERED] = "LASER_RECOVERED SSSL",
        [GATEWAY_DIAG_CONFIG_MISMATCH] = "CONFIG_MISMATCH SLSL",
    };
    return event < GATEWAY_DIAG_EVENT_COUNT ? names[event] : "UNKNOWN";
}

static const char *TAG = "GW_OUTPUT";
static QueueHandle_t s_events;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_network;
static bool s_mqtt;
static bool s_ap_active;
static gateway_output_snapshot_t s_snapshot;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint8_t bit(bsp_do_channel_t channel)
{
    return (uint8_t)(1U << (uint8_t)channel);
}

static void commit_output(uint8_t desired, uint8_t *rendered)
{
    if (desired == *rendered) return;
    const esp_err_t error = bsp_do_write_mask(desired);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 output write failed: %s", esp_err_to_name(error));
        return;
    }
    *rendered = desired;
    const gateway_io_mapping_t *io = gateway_io_get_mapping();
    gateway_output_snapshot_t snapshot = {
        .buzzer = (desired & bit(io->buzzer)) != 0,
        .tower_red = (desired & bit(io->tower_red)) != 0,
        .tower_yellow = (desired & bit(io->tower_yellow)) != 0,
        .tower_green = (desired & bit(io->tower_green)) != 0,
        .ap_active = (desired & bit(io->ap_status)) != 0,
    };
    taskENTER_CRITICAL(&s_lock);
    snapshot.production_network = s_network;
    snapshot.mqtt_connected = s_mqtt;
    s_snapshot = snapshot;
    taskEXIT_CRITICAL(&s_lock);
}

static void output_task(void *argument)
{
    (void)argument;
    const gateway_io_mapping_t *io = gateway_io_get_mapping();
    uint8_t rendered = bsp_do_get_active_mask();
    bool tower_phase = true, buzzer_on = false;
    uint32_t tower_deadline = now_ms() + TOWER_BLINK_MS, buzzer_deadline = 0;
    const buzzer_pattern_t *pattern = NULL;
    uint8_t step = 0;
    bool onboard_rendered = false;

    for (;;) {
        gateway_diagnostic_event_t event;
        /* Phát hết một mã rồi mới lấy sự kiện kế tiếp. Nếu nhiều trạng thái đổi
         * cùng lúc, các mã được phát theo thứ tự thay vì ghi đè sau 20 ms. */
        const BaseType_t received = pattern == NULL
            ? xQueueReceive(s_events, &event, pdMS_TO_TICKS(OUTPUT_TICK_MS))
            : pdFALSE;
        if (received == pdTRUE) {
            pattern = &PATTERNS[event];
            step = 0;
            buzzer_on = pattern->count > 0;
            buzzer_deadline = now_ms() + (buzzer_on ? pattern->steps[0].on_ms : 0U);
            ESP_LOGI(TAG, "Buzzer code: %s", event_name(event));
        } else if (pattern != NULL) {
            vTaskDelay(pdMS_TO_TICKS(OUTPUT_TICK_MS));
        }
        const uint32_t now = now_ms();
        if (pattern != NULL && (int32_t)(now - buzzer_deadline) >= 0) {
            if (buzzer_on) {
                buzzer_on = false;
                if (pattern->steps[step].off_ms == 0U) pattern = NULL;
                else buzzer_deadline = now + pattern->steps[step].off_ms;
            } else if (++step < pattern->count) {
                buzzer_on = true;
                buzzer_deadline = now + pattern->steps[step].on_ms;
            } else pattern = NULL;
        }

        bool network, mqtt, ap_active;
        taskENTER_CRITICAL(&s_lock);
        network = s_network;
        mqtt = s_mqtt;
        ap_active = s_ap_active;
        taskEXIT_CRITICAL(&s_lock);
        const bool blinking = !network || !mqtt;
        if (blinking && (int32_t)(now - tower_deadline) >= 0) {
            tower_phase = !tower_phase;
            tower_deadline = now + TOWER_BLINK_MS;
        } else if (!blinking) tower_phase = true;

        uint8_t desired = buzzer_on ? bit(io->buzzer) : 0U;
        if (!network) {
            if (tower_phase) desired |= bit(io->tower_red);
        } else {
            desired |= bit(io->tower_green);
            if (!mqtt && tower_phase) desired |= bit(io->tower_yellow);
        }
        /* DO8 is the solid local configuration-AP LED, same policy as Callbox. */
        if (ap_active) desired |= bit(io->ap_status);
        commit_output(desired, &rendered);
        /* DO1 drives the tower buzzer and GPIO46 drives the onboard buzzer.
         * Both are rendered from the same state machine so every pulse starts
         * and ends in the same output tick. */
        if (buzzer_on != onboard_rendered) {
            const esp_err_t error = buzzer_on
                ? bsp_buzzer_set(ONBOARD_BUZZER_HZ, ONBOARD_BUZZER_DUTY)
                : bsp_buzzer_off();
            if (error == ESP_OK) {
                onboard_rendered = buzzer_on;
            } else {
                ESP_LOGE(TAG, "Onboard buzzer update failed: %s",
                         esp_err_to_name(error));
            }
        }
    }
}

esp_err_t gateway_output_start(void)
{
    if (s_events != NULL) return ESP_OK;
    if (bsp_do_all_off() != ESP_OK) return ESP_FAIL;
    if (bsp_buzzer_off() != ESP_OK) return ESP_FAIL;
    s_events = xQueueCreate(OUTPUT_QUEUE_DEPTH, sizeof(gateway_diagnostic_event_t));
    if (s_events == NULL ||
        xTaskCreate(output_task, "gw_output", 3072, NULL, 6, NULL) != pdPASS) {
        if (s_events != NULL) { vQueueDelete(s_events); s_events = NULL; }
        (void)bsp_do_all_off();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Single output owner ready: DO1 + onboard buzzer synchronized, DO2 red, DO3 yellow, DO4 green, DO8 AP");
    return ESP_OK;
}

void gateway_output_set_health(bool production_network, bool mqtt_connected,
                               bool ap_active)
{
    taskENTER_CRITICAL(&s_lock);
    s_network = production_network;
    s_mqtt = mqtt_connected;
    s_ap_active = ap_active;
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t gateway_output_report(gateway_diagnostic_event_t event)
{
    if (s_events == NULL || event >= GATEWAY_DIAG_EVENT_COUNT) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Buzzer event queue full; event %u skipped", (unsigned)event);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void gateway_output_snapshot(gateway_output_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    taskENTER_CRITICAL(&s_lock);
    *snapshot = s_snapshot;
    taskEXIT_CRITICAL(&s_lock);
}
