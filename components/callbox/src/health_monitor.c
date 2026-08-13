/**
 * @file    health_monitor.c
 * @brief   Application health supervisor and ESP Task Watchdog bridge.
 */
#include "health_monitor.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "bsp_do.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HEALTH_MONITOR";

#define HEALTH_MONITOR_PERIOD_MS       1000U
#define HEALTH_MONITOR_TELEMETRY_MS   60000U
#define HEALTH_RESTART_LOG_FLUSH_MS     500U
#define HEALTH_RTC_FAULT_MAGIC    0x484C5448UL /* "HLTH" */
#define HEALTH_RTC_BOOT_MAGIC     0x42544C50UL /* "BTLP" */
#define HEALTH_RECOVERY_THRESHOLD       3U
#define HEALTH_MAX_FAILURE_STREAK       8U
#define HEALTH_MAX_BOOT_BACKOFF_MS   60000U

typedef struct {
    TickType_t last_check_in;
    TickType_t last_stack_sample;
    UBaseType_t stack_min_free_bytes;
    bool seen;
} health_slot_t;

static health_slot_t s_slots[HEALTH_TASK_COUNT];
static portMUX_TYPE s_health_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_monitor_task;
static health_monitor_mode_t s_monitor_mode = HEALTH_MONITOR_MODE_NORMAL;
static bool s_boot_checked;
static bool s_recovery_requested;

/* RTC slow memory survives software/watchdog reset without wearing NVS. */
RTC_NOINIT_ATTR static uint32_t s_rtc_fault_magic;
RTC_NOINIT_ATTR static uint32_t s_rtc_fault_task;
RTC_NOINIT_ATTR static uint32_t s_rtc_boot_magic;
RTC_NOINIT_ATTR static uint32_t s_rtc_failure_streak;

static const char *const s_task_names[HEALTH_TASK_COUNT] = {
    [HEALTH_TASK_IO_HANDLER] = "io_handler",
    [HEALTH_TASK_STATE_MACHINE] = "state_machine",
    [HEALTH_TASK_MQTT_SUPERVISOR] = "mqtt_comm",
    [HEALTH_TASK_MQTT_TX] = "mqtt_tx",
    [HEALTH_TASK_OUTPUT_RENDERER] = "output_renderer",
    [HEALTH_TASK_NETWORK_STATUS] = "network_status",
    [HEALTH_TASK_WIFI_SELECT] = "wifi_select",
};

static bool reset_reason_preserves_failure_streak(esp_reset_reason_t reason)
{
    /* A generic software reset (OTA/operator/recovery trial) is not evidence
     * of a crash. Count it only when our controlled-restart marker is present. */
    return (reason == ESP_RST_SW && s_rtc_fault_magic == HEALTH_RTC_FAULT_MAGIC) ||
           reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT;
}

void health_monitor_boot_begin(void)
{
    if (s_boot_checked) return;
    s_boot_checked = true;

    const esp_reset_reason_t reason = esp_reset_reason();
    if (s_rtc_boot_magic != HEALTH_RTC_BOOT_MAGIC ||
        !reset_reason_preserves_failure_streak(reason)) {
        /* Power-on, brown-out and external reset are fresh starts. Random or
         * stale RTC_NOINIT contents must never latch the board in recovery. */
        s_rtc_boot_magic = HEALTH_RTC_BOOT_MAGIC;
        s_rtc_failure_streak = 0U;
        s_rtc_fault_magic = 0U;
        s_rtc_fault_task = 0U;
    } else if (s_rtc_failure_streak < HEALTH_MAX_FAILURE_STREAK) {
        ++s_rtc_failure_streak;
    }

    s_recovery_requested = s_rtc_failure_streak >= HEALTH_RECOVERY_THRESHOLD;
    const uint32_t shift = s_rtc_failure_streak == 0U
                               ? 0U
                               : (s_rtc_failure_streak > 6U
                                      ? 6U
                                      : s_rtc_failure_streak - 1U);
    const uint32_t raw_backoff_ms = s_rtc_failure_streak == 0U
                                        ? 0U
                                        : (1000U << shift);
    const uint32_t backoff_ms = raw_backoff_ms > HEALTH_MAX_BOOT_BACKOFF_MS
                                    ? HEALTH_MAX_BOOT_BACKOFF_MS
                                    : raw_backoff_ms;

    ESP_LOGW(TAG, "Reset reason=%d, local failure streak=%" PRIu32 ", recovery=%s",
             (int)reason, s_rtc_failure_streak,
             s_recovery_requested ? "requested" : "no");
    if (backoff_ms != 0U) {
        ESP_LOGW(TAG, "Applying bounded boot backoff of %" PRIu32 " ms", backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
    }
}

bool health_monitor_recovery_requested(void)
{
    return s_recovery_requested;
}

static void health_controlled_restart(health_task_id_t task_id, uint32_t age_ms)
{
    ESP_LOGE(TAG, "Critical task stalled: %s, no check-in for %" PRIu32 " ms",
             s_task_names[task_id], age_ms);

    s_rtc_fault_task = (uint32_t)task_id;
    s_rtc_fault_magic = HEALTH_RTC_FAULT_MAGIC;

    /* Flush the diagnostic, then put active-low outputs in a known safe state
     * immediately before reset so renderer cannot turn them back on. */
    vTaskDelay(pdMS_TO_TICKS(HEALTH_RESTART_LOG_FLUSH_MS));
    const esp_err_t safe_err = bsp_do_all_off();
    if (safe_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not force all outputs OFF before restart: %s",
                 esp_err_to_name(safe_err));
    } else {
        ESP_LOGW(TAG, "All outputs forced OFF before controlled restart");
    }
    esp_restart();
}

static void health_log_telemetry(void)
{
    bool seen[HEALTH_TASK_COUNT] = { false };
    UBaseType_t stack_free[HEALTH_TASK_COUNT] = { 0 };

    ESP_LOGI(TAG, "Heap free=%u bytes, minimum-ever=%u bytes, largest-8bit=%u bytes",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    /* Do not retain TaskHandle_t for later telemetry: a finite task can delete
     * itself, leaving a stale handle. Each worker samples its own stack. */
    portENTER_CRITICAL(&s_health_lock);
    for (int i = 0; i < HEALTH_TASK_COUNT; ++i) {
        seen[i] = s_slots[i].seen;
        stack_free[i] = s_slots[i].stack_min_free_bytes;
    }
    portEXIT_CRITICAL(&s_health_lock);

    for (int i = 0; i < HEALTH_TASK_COUNT; ++i) {
        if (seen[i]) {
            ESP_LOGI(TAG, "Task %-15s stack minimum free=%u bytes",
                     s_task_names[i], (unsigned)stack_free[i]);
        }
    }
}

static void health_clear_failure_streak(void)
{
    s_rtc_failure_streak = 0U;
    s_rtc_fault_magic = 0U;
    s_rtc_fault_task = 0U;
    s_recovery_requested = false;
}

static void health_watchdog_fault_restart(const char *reason) __attribute__((noreturn));
static void health_watchdog_fault_restart(const char *reason)
{
    /* Watchdog API failure itself is a local health fault. Mark it before the
     * software reset so boot_begin counts it and the breaker can stop loops. */
    s_rtc_fault_task = UINT32_MAX;
    s_rtc_fault_magic = HEALTH_RTC_FAULT_MAGIC;
    ESP_LOGE(TAG, "Watchdog infrastructure fault: %s", reason);
    vTaskDelay(pdMS_TO_TICKS(HEALTH_RESTART_LOG_FLUSH_MS));
    (void)bsp_do_all_off();
    esp_restart();
    __builtin_unreachable();
}

static void health_monitor_task(void *arg)
{
    (void)arg;
    const TickType_t started_at = xTaskGetTickCount();
    TickType_t last_telemetry = started_at;
    bool stable_window_handled = false;

    /* Only the supervisor subscribes directly to TWDT. Named workers use
     * heartbeats, which lets us log the exact stalled task before reset. */
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot subscribe health supervisor to TWDT: %s",
                 esp_err_to_name(err));
        health_watchdog_fault_restart("esp_task_wdt_add");
    }

    ESP_LOGI(TAG, "Health supervisor active (timeout=%u ms, grace=%u ms, mode=%s)",
             HEALTH_MONITOR_TASK_TIMEOUT_MS, HEALTH_MONITOR_STARTUP_GRACE_MS,
             s_monitor_mode == HEALTH_MONITOR_MODE_RECOVERY ? "recovery" : "normal");

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        err = esp_task_wdt_reset();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot feed Task Watchdog: %s", esp_err_to_name(err));
            health_watchdog_fault_restart("esp_task_wdt_reset");
        }

        if ((now - started_at) >= pdMS_TO_TICKS(HEALTH_MONITOR_STARTUP_GRACE_MS)) {
            for (int i = 0; i < HEALTH_TASK_COUNT; ++i) {
                TickType_t last = 0;
                bool seen = false;
                portENTER_CRITICAL(&s_health_lock);
                seen = s_slots[i].seen;
                last = s_slots[i].last_check_in;
                portEXIT_CRITICAL(&s_health_lock);

                /* Unstarted workers are intentionally not monitored. This is
                 * required in recovery mode, where business tasks are omitted. */
                if (seen && (now - last) > pdMS_TO_TICKS(HEALTH_MONITOR_TASK_TIMEOUT_MS)) {
                    health_controlled_restart((health_task_id_t)i,
                                              (uint32_t)((now - last) * portTICK_PERIOD_MS));
                }
            }
        }

        if ((now - last_telemetry) >= pdMS_TO_TICKS(HEALTH_MONITOR_TELEMETRY_MS)) {
            health_log_telemetry();
            last_telemetry = now;
        }

        if (!stable_window_handled &&
            (now - started_at) >= pdMS_TO_TICKS(HEALTH_MONITOR_STABLE_WINDOW_MS)) {
            stable_window_handled = true;
            health_clear_failure_streak();
            if (s_monitor_mode == HEALTH_MONITOR_MODE_RECOVERY) {
                /* Recovery is not a permanent trap. A healthy AP/WebUI runtime
                 * gets one deterministic full-application trial every 10 min. */
                ESP_LOGW(TAG, "Recovery AP/WebUI stable for 10 minutes; retrying full app");
                vTaskDelay(pdMS_TO_TICKS(HEALTH_RESTART_LOG_FLUSH_MS));
                esp_restart();
            } else {
                ESP_LOGI(TAG, "Full runtime stable for 10 minutes; failure streak cleared");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(HEALTH_MONITOR_PERIOD_MS));
    }
}

esp_err_t health_monitor_init(health_monitor_mode_t mode)
{
    if (s_monitor_task) return ESP_OK;
    s_monitor_mode = mode;

    if (s_rtc_fault_magic == HEALTH_RTC_FAULT_MAGIC) {
        const uint32_t task = s_rtc_fault_task;
        ESP_LOGE(TAG, "Previous restart was caused by stalled task: %s",
                 task < HEALTH_TASK_COUNT ? s_task_names[task] : "unknown");
    }

    const esp_task_wdt_config_t twdt_config = {
        .timeout_ms = HEALTH_MONITOR_TASK_TIMEOUT_MS,
        .idle_core_mask = (1U << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1U,
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&twdt_config);
    if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_init(&twdt_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot configure Task Watchdog: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(health_monitor_task, "health_monitor", 3072, NULL, 11,
                    &s_monitor_task) != pdPASS) {
        s_monitor_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void health_monitor_check_in(health_task_id_t task_id)
{
    if ((unsigned)task_id >= (unsigned)HEALTH_TASK_COUNT) return;

    const TickType_t now = xTaskGetTickCount();
    bool sample_stack;
    portENTER_CRITICAL(&s_health_lock);
    sample_stack = !s_slots[task_id].seen ||
                   (now - s_slots[task_id].last_stack_sample) >=
                       pdMS_TO_TICKS(HEALTH_MONITOR_TELEMETRY_MS);
    portEXIT_CRITICAL(&s_health_lock);

    /* ESP-IDF reports this watermark in bytes. Sampling once per minute avoids
     * scanning a high-frequency worker stack on every check-in. */
    const UBaseType_t stack_free = sample_stack ? uxTaskGetStackHighWaterMark(NULL) : 0U;

    portENTER_CRITICAL(&s_health_lock);
    s_slots[task_id].last_check_in = now;
    s_slots[task_id].seen = true;
    if (sample_stack) {
        s_slots[task_id].last_stack_sample = now;
        s_slots[task_id].stack_min_free_bytes = stack_free;
    }
    portEXIT_CRITICAL(&s_health_lock);
}

void health_monitor_force_restart(const char *reason)
{
    ESP_LOGE(TAG, "Controlled restart requested: %s", reason ? reason : "unspecified");
    s_rtc_fault_task = UINT32_MAX;
    s_rtc_fault_magic = HEALTH_RTC_FAULT_MAGIC;
    vTaskDelay(pdMS_TO_TICKS(HEALTH_RESTART_LOG_FLUSH_MS));
    const esp_err_t safe_err = bsp_do_all_off();
    if (safe_err != ESP_OK && safe_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Final safe-output write failed: %s", esp_err_to_name(safe_err));
    }
    esp_restart();
    __builtin_unreachable();
}
