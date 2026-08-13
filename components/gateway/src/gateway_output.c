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
#define ONBOARD_BUZZER_DUTY 50U

typedef struct {
    uint16_t frequency_hz;
    uint16_t on_ms;
    uint16_t off_ms;
} buzzer_step_t;

typedef enum {
    INDICATOR_AP = 0,
    INDICATOR_GREEN,
    INDICATOR_YELLOW,
    INDICATOR_RED,
    INDICATOR_RED_YELLOW,
} event_indicator_t;

typedef struct {
    const buzzer_step_t *steps;
    uint8_t count;
    event_indicator_t indicator;
} buzzer_pattern_t;

#define NOTE(hz, on, off) { (hz), (on), (off) }
/* Giai điệu hiện trường dùng cả hướng cao độ và tiết tấu. Còi DO1 theo đúng
 * nhịp bật/tắt; buzzer thụ động onboard bổ sung cao độ để nhận biết tức thì. */
static const buzzer_step_t P_AP_ON[] = {
    NOTE(2600, 100, 80), NOTE(2600, 100, 80), NOTE(2600, 100, 400),
}; /* ba tiếng cao nhanh */
static const buzzer_step_t P_AP_OFF[] = {
    NOTE(900, 300, 120), NOTE(900, 300, 400),
}; /* hai tiếng trầm chậm */
static const buzzer_step_t P_NET_UP[] = {
    NOTE(1200, 150, 80), NOTE(2400, 220, 400),
}; /* hai nốt đi lên */
static const buzzer_step_t P_NET_DOWN[] = {
    NOTE(2400, 150, 80), NOTE(1200, 300, 400),
}; /* hai nốt đi xuống */
static const buzzer_step_t P_MQTT_UP[] = {
    NOTE(900, 130, 70), NOTE(1600, 130, 70), NOTE(2600, 220, 400),
}; /* ba nốt đi lên */
static const buzzer_step_t P_MQTT_DOWN[] = {
    NOTE(2600, 130, 70), NOTE(1600, 130, 70), NOTE(900, 300, 400),
}; /* ba nốt đi xuống */
static const buzzer_step_t P_CAN_OFF[] = {
    NOTE(750, 230, 80), NOTE(2500, 230, 80),
    NOTE(750, 230, 80), NOTE(2500, 230, 400),
}; /* còi cảnh báo thấp-cao lặp lại */
static const buzzer_step_t P_CAN_RECOVERED[] = {
    NOTE(2400, 120, 80), NOTE(1000, 120, 80), NOTE(2400, 220, 400),
}; /* cao-thấp-cao */
static const buzzer_step_t P_LASER_OFF[] = {
    NOTE(800, 260, 100), NOTE(800, 260, 100), NOTE(800, 260, 400),
}; /* ba tiếng trầm chậm */
static const buzzer_step_t P_LASER_RECOVERED[] = {
    NOTE(2700, 100, 80), NOTE(2700, 100, 80), NOTE(2700, 160, 400),
}; /* ba tiếng cao nhanh */
static const buzzer_step_t P_MISMATCH[] = {
    NOTE(2600, 140, 80), NOTE(800, 140, 80),
    NOTE(2600, 140, 80), NOTE(800, 220, 400),
}; /* cao-thấp lặp lại */

#define PATTERN(steps, light) \
    { (steps), (uint8_t)(sizeof(steps) / sizeof((steps)[0])), (light) }
static const buzzer_pattern_t PATTERNS[GATEWAY_DIAG_EVENT_COUNT] = {
    [GATEWAY_DIAG_AP_ON] = PATTERN(P_AP_ON, INDICATOR_AP),
    [GATEWAY_DIAG_AP_OFF] = PATTERN(P_AP_OFF, INDICATOR_AP),
    [GATEWAY_DIAG_NETWORK_UP] = PATTERN(P_NET_UP, INDICATOR_GREEN),
    [GATEWAY_DIAG_NETWORK_DOWN] = PATTERN(P_NET_DOWN, INDICATOR_RED),
    [GATEWAY_DIAG_MQTT_UP] = PATTERN(P_MQTT_UP, INDICATOR_GREEN),
    [GATEWAY_DIAG_MQTT_DOWN] = PATTERN(P_MQTT_DOWN, INDICATOR_YELLOW),
    [GATEWAY_DIAG_CAN_BUS_OFF] = PATTERN(P_CAN_OFF, INDICATOR_RED),
    [GATEWAY_DIAG_CAN_RECOVERED] = PATTERN(P_CAN_RECOVERED, INDICATOR_GREEN),
    [GATEWAY_DIAG_LASER_OFFLINE] = PATTERN(P_LASER_OFF, INDICATOR_YELLOW),
    [GATEWAY_DIAG_LASER_RECOVERED] = PATTERN(P_LASER_RECOVERED, INDICATOR_GREEN),
    [GATEWAY_DIAG_CONFIG_MISMATCH] = PATTERN(P_MISMATCH, INDICATOR_RED_YELLOW),
};

static const char *event_name(gateway_diagnostic_event_t event)
{
    static const char *const names[GATEWAY_DIAG_EVENT_COUNT] = {
        [GATEWAY_DIAG_AP_ON] = "AP_ON HIGH_X3",
        [GATEWAY_DIAG_AP_OFF] = "AP_OFF LOW_X2",
        [GATEWAY_DIAG_NETWORK_UP] = "NETWORK_UP ASCEND_X2",
        [GATEWAY_DIAG_NETWORK_DOWN] = "NETWORK_DOWN DESCEND_X2",
        [GATEWAY_DIAG_MQTT_UP] = "MQTT_UP ASCEND_X3",
        [GATEWAY_DIAG_MQTT_DOWN] = "MQTT_DOWN DESCEND_X3",
        [GATEWAY_DIAG_CAN_BUS_OFF] = "CAN_BUS_OFF SIREN",
        [GATEWAY_DIAG_CAN_RECOVERED] = "CAN_RECOVERED HIGH_LOW_HIGH",
        [GATEWAY_DIAG_LASER_OFFLINE] = "LASER_OFFLINE LOW_X3",
        [GATEWAY_DIAG_LASER_RECOVERED] = "LASER_RECOVERED HIGH_X3",
        [GATEWAY_DIAG_CONFIG_MISMATCH] = "CONFIG_MISMATCH HIGH_LOW_X2",
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

static uint8_t event_indicator_mask(event_indicator_t indicator,
                                    const gateway_io_mapping_t *io)
{
    switch (indicator) {
    case INDICATOR_AP: return bit(io->ap_status);
    case INDICATOR_GREEN: return bit(io->tower_green);
    case INDICATOR_YELLOW: return bit(io->tower_yellow);
    case INDICATOR_RED: return bit(io->tower_red);
    case INDICATOR_RED_YELLOW:
        return (uint8_t)(bit(io->tower_red) | bit(io->tower_yellow));
    default: return 0U;
    }
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
    uint16_t onboard_frequency_hz = 0U;

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
        if (pattern != NULL) {
            /* Trong lúc phát mã sự kiện, tháp đèn chuyển sang màu của sự kiện
             * và chớp đúng theo từng tiếng còi. Sau khoảng nghỉ cuối, pattern
             * kết thúc và đèn tự quay lại trạng thái hệ thống bên dưới. */
            if (buzzer_on) desired |= event_indicator_mask(pattern->indicator, io);
        } else {
            if (!network) {
                if (tower_phase) desired |= bit(io->tower_red);
            } else {
                desired |= bit(io->tower_green);
                if (!mqtt && tower_phase) desired |= bit(io->tower_yellow);
            }
        }
        /* DO8 sáng liên tục khi AP bật, trừ lúc chính DO8 đang phát mã AP. */
        if (ap_active && (pattern == NULL || pattern->indicator != INDICATOR_AP)) {
            desired |= bit(io->ap_status);
        }
        commit_output(desired, &rendered);
        /* DO1 drives the tower buzzer and GPIO46 drives the onboard buzzer.
         * Both are rendered from the same state machine so every pulse starts
         * and ends in the same output tick. */
        const uint16_t desired_frequency_hz = buzzer_on && pattern != NULL
            ? pattern->steps[step].frequency_hz : 0U;
        if (buzzer_on != onboard_rendered ||
            (buzzer_on && desired_frequency_hz != onboard_frequency_hz)) {
            const esp_err_t error = buzzer_on
                ? bsp_buzzer_set(desired_frequency_hz, ONBOARD_BUZZER_DUTY)
                : bsp_buzzer_off();
            if (error == ESP_OK) {
                onboard_rendered = buzzer_on;
                onboard_frequency_hz = desired_frequency_hz;
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
