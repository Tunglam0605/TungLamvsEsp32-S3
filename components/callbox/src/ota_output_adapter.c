#include "ota_output_adapter.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "ota_events.h"
#include "ota_service.h"
#include "status.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_output_snapshot_t s_snapshot;
static bool s_initialized;

static bool is_tower_override_state(ota_state_t state)
{
    return state == OTA_STATE_RECEIVING ||
           state == OTA_STATE_VERIFYING ||
           state == OTA_STATE_INSTALLING ||
           state == OTA_STATE_REBOOT_PENDING;
}

static void ota_event_callback(const ota_event_t *event, void *context)
{
    (void)context;
    if (!event) return;

    portENTER_CRITICAL(&s_lock);
    const ota_state_t previous = s_snapshot.ota.state;
    s_snapshot.ota = event->status;
    s_snapshot.tower_override_active = is_tower_override_state(event->status.state);
    portEXIT_CRITICAL(&s_lock);

    if (previous == event->status.state) return;
    switch (event->status.state) {
    case OTA_STATE_STAGED:
        status_request_feedback(OUTPUT_FEEDBACK_OTA_STAGED);
        break;
    case OTA_STATE_INSTALLING:
    case OTA_STATE_REBOOT_PENDING:
        status_request_feedback(OUTPUT_FEEDBACK_OTA_INSTALLING);
        break;
    case OTA_STATE_FAILED:
        status_request_feedback(OUTPUT_FEEDBACK_OTA_FAILED);
        break;
    default:
        break;
    }
}

esp_err_t ota_output_adapter_init(void)
{
    if (s_initialized) return ESP_OK;
    ota_status_t current;
    esp_err_t err = ota_service_get_status(&current);
    if (err != ESP_OK) return err;
    portENTER_CRITICAL(&s_lock);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.ota = current;
    s_snapshot.tower_override_active = is_tower_override_state(current.state);
    portEXIT_CRITICAL(&s_lock);
    err = ota_service_set_event_callback(ota_event_callback, NULL);
    if (err == ESP_OK) s_initialized = true;
    return err;
}

void ota_output_adapter_get_snapshot(ota_output_snapshot_t *out_snapshot)
{
    if (!out_snapshot) return;
    portENTER_CRITICAL(&s_lock);
    *out_snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
}
