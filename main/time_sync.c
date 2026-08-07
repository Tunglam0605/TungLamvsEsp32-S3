/**
 * @file time_sync.c
 * @brief Keeps the ESP32 RTC synchronized for TCP and TLS MQTT operation.
 */
#include "time_sync.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "TIME_SYNC";
static bool s_started;
static char s_primary[64];
static char s_fallback[64];

static void time_sync_start(const Config_t *config)
{
    const char *primary = config && config->sntp_primary[0] ? config->sntp_primary : "pool.ntp.org";
    const char *fallback = config && config->sntp_fallback[0] ? config->sntp_fallback : "time.google.com";
    /* esp_sntp retains the server pointers. Never point it at a temporary
     * Config_t copy from the HTTP save handler. */
    strncpy(s_primary, primary, sizeof(s_primary) - 1U);
    s_primary[sizeof(s_primary) - 1U] = '\0';
    strncpy(s_fallback, fallback, sizeof(s_fallback) - 1U);
    s_fallback[sizeof(s_fallback) - 1U] = '\0';
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_primary);
    esp_sntp_setservername(1, s_fallback);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started: primary=%s fallback=%s", s_primary, s_fallback);
}

void time_sync_init(void)
{
    if (s_started) return;

    /* Local presentation is ICT; MQTT still transmits Unix UTC epoch time. */
    setenv("TZ", "ICT-7", 1);
    tzset();

    /* SNTP remains active and retries automatically when STA or Ethernet
     * becomes available. Both MQTT modes therefore use real timestamps. */
    time_sync_start(&g_config);
    s_started = true;
}

bool time_sync_is_valid(void)
{
    return time(NULL) >= 1704067200; /* 2024-01-01 UTC */
}

void time_sync_reconfigure(const Config_t *config)
{
    if (!config) return;
    if (s_started) esp_sntp_stop();
    time_sync_start(config);
    s_started = true;
}
