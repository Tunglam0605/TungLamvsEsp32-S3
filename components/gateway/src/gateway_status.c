#include "gateway_status.h"

#include "bsp_can.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "gateway_output.h"
#include "laser_can_bringup.h"
#include "platform_wifi.h"
#include "warehouse_manager.h"

static const char *TAG = "GW_STATUS";
#define LASER_STARTUP_SETTLE_MS 10000LL
/* A group reload deliberately makes its Lasers restart before they ask for
 * their DLC8 configuration.  Do not turn that expected commissioning gap
 * into a red-tower / offline alarm.  CAN and network faults are unaffected. */
#define LASER_CONFIG_RESTART_GRACE_MS 8000LL

esp_err_t gateway_diagnostic_report(gateway_diagnostic_event_t event)
{
    return gateway_output_report(event);
}

static void report_transition(bool current, bool *previous,
                              gateway_diagnostic_event_t up,
                              gateway_diagnostic_event_t down)
{
    if (current == *previous) return;
    (void)gateway_output_report(current ? up : down);
    *previous = current;
}

static bool laser_config_mismatch_present(void)
{
    laser_can_node_status_t node;
    for (uint8_t id = LASER_ID_MIN; id <= LASER_ID_MAX; ++id) {
        if (laser_can_bringup_get_node(id, &node) &&
            node.config_state == LASER_CONFIG_MISMATCH) return true;
    }
    return false;
}

static bool laser_config_restart_in_progress(void)
{
    laser_can_node_status_t node;
    for (uint8_t id = LASER_ID_MIN; id <= LASER_ID_MAX; ++id) {
        if (!laser_can_bringup_get_node(id, &node) || !node.config_managed) continue;
        if (node.config_state == LASER_CONFIG_PENDING ||
            node.config_state == LASER_CONFIG_SENT) return true;
    }
    return false;
}

static void status_task(void *argument)
{
    (void)argument;
    bool initialized = false, network = false, mqtt = false, ap = false;
    bool can_ok = true, laser_ok = false, mismatch = false;
    bool laser_monitor_armed = false;
    bool config_restart_seen = false;
    int64_t laser_alarm_suppressed_until_ms = 0;
    const int64_t started_ms = esp_timer_get_time() / 1000LL;
    for (;;) {
        bsp_can_status_t can = {0};
        warehouse_snapshot_t warehouse;
        bsp_can_get_status(&can);
        warehouse_manager_snapshot(&warehouse);
        const bool next_network = gateway_network_production_available();
        const bool next_mqtt = gateway_mqtt_is_connected();
        const bool next_ap = platform_wifi_ap_is_active();
        const bool next_can = can.state != BSP_CAN_STATE_BUS_OFF &&
                              can.state != BSP_CAN_STATE_STOPPED;
        const bool next_can_healthy = can.state == BSP_CAN_STATE_ACTIVE;
        const bool next_laser = warehouse.configured == 0 ||
                                warehouse.online == warehouse.configured;
        const bool next_mismatch = laser_config_mismatch_present();
        const bool config_restart = laser_config_restart_in_progress();
        const int64_t now_ms = esp_timer_get_time() / 1000LL;
        if (config_restart && !config_restart_seen) {
            laser_alarm_suppressed_until_ms = now_ms + LASER_CONFIG_RESTART_GRACE_MS;
            ESP_LOGI(TAG, "Laser offline alarm muted for %lld ms during configuration reload",
                     LASER_CONFIG_RESTART_GRACE_MS);
        }
        config_restart_seen = config_restart;
        const bool laser_alarm_suppressed = now_ms < laser_alarm_suppressed_until_ms;

        gateway_output_set_health(next_network, next_mqtt, next_ap, next_can,
                                  next_can_healthy,
                                  next_laser || laser_alarm_suppressed,
                                  !next_mismatch);
        if (!initialized) {
            network = next_network;
            mqtt = next_mqtt;
            ap = next_ap;
            can_ok = next_can;
            laser_ok = next_laser;
            mismatch = next_mismatch;
            initialized = true;
        } else {
            report_transition(next_network, &network, GATEWAY_DIAG_NETWORK_UP,
                              GATEWAY_DIAG_NETWORK_DOWN);
            report_transition(next_mqtt, &mqtt, GATEWAY_DIAG_MQTT_UP,
                              GATEWAY_DIAG_MQTT_DOWN);
            report_transition(next_ap, &ap, GATEWAY_DIAG_AP_ON,
                              GATEWAY_DIAG_AP_OFF);
            report_transition(next_can, &can_ok, GATEWAY_DIAG_CAN_RECOVERED,
                              GATEWAY_DIAG_CAN_BUS_OFF);
            if (!laser_monitor_armed) {
                laser_ok = next_laser;
                laser_monitor_armed = esp_timer_get_time() / 1000LL - started_ms >=
                                      LASER_STARTUP_SETTLE_MS;
                if (laser_monitor_armed) {
                    ESP_LOGI(TAG, "Laser alarms armed after startup restore settle time");
                }
            } else if (laser_alarm_suppressed) {
                /* Hold the previous healthy state through the intentional
                 * Laser reboot.  If it never returns, normal offline alarm
                 * processing resumes once the grace period expires. */
                laser_ok = true;
            } else {
                report_transition(next_laser, &laser_ok,
                                  GATEWAY_DIAG_LASER_RECOVERED,
                                  GATEWAY_DIAG_LASER_OFFLINE);
            }
            if (next_mismatch && !mismatch)
                (void)gateway_output_report(GATEWAY_DIAG_CONFIG_MISMATCH);
            mismatch = next_mismatch;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t gateway_status_start(void)
{
    if (xTaskCreate(status_task, "gw_status", 4096, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Health transitions feed the single physical output owner");
    return ESP_OK;
}
