#include "gateway_status.h"

#include "bsp_can.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "gateway_output.h"
#include "laser_can_bringup.h"
#include "platform_wifi.h"
#include "warehouse_manager.h"

static const char *TAG = "GW_STATUS";

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

static void status_task(void *argument)
{
    (void)argument;
    bool initialized = false, network = false, mqtt = false, ap = false;
    bool can_ok = true, laser_ok = false, mismatch = false;
    for (;;) {
        bsp_can_status_t can = {0};
        warehouse_snapshot_t warehouse;
        bsp_can_get_status(&can);
        warehouse_manager_snapshot(&warehouse);
        const bool next_network = gateway_network_production_available();
        const bool next_mqtt = gateway_mqtt_is_connected();
        const bool next_ap = platform_wifi_ap_is_active();
        const bool next_can = can.state != BSP_CAN_STATE_BUS_OFF;
        const bool next_laser = warehouse.configured == 0 ||
                                warehouse.online == warehouse.configured;
        const bool next_mismatch = laser_config_mismatch_present();

        gateway_output_set_health(next_network, next_mqtt);
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
            report_transition(next_laser, &laser_ok, GATEWAY_DIAG_LASER_RECOVERED,
                              GATEWAY_DIAG_LASER_OFFLINE);
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
