#ifndef CALLBOX_OTA_OUTPUT_ADAPTER_H
#define CALLBOX_OTA_OUTPUT_ADAPTER_H

#include <stdbool.h>
#include "esp_err.h"
#include "ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ota_status_t ota;
    bool tower_override_active;
} ota_output_snapshot_t;

esp_err_t ota_output_adapter_init(void);
void ota_output_adapter_get_snapshot(ota_output_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* CALLBOX_OTA_OUTPUT_ADAPTER_H */
