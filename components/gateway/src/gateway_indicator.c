#include "gateway_indicator.h"
#include "esp_log.h"

esp_err_t gateway_indicator_start(const gateway_indicator_config_t *config)
{
    if (config == NULL || !config->mapping_confirmed) {
        ESP_LOGW("GW_INDICATOR", "Tower DO mapping is not confirmed; indicator stays disabled (TBD)");
        return ESP_ERR_NOT_SUPPORTED;
    }
    /* Mapping-dependent state task is intentionally not enabled until the
     * physical RED/YELLOW/GREEN DO channels are confirmed in product config. */
    return ESP_ERR_NOT_SUPPORTED;
}
