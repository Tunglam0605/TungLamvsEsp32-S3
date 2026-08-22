#ifndef CALLBOX_OTA_BOOT_VALIDATOR_H
#define CALLBOX_OTA_BOOT_VALIDATOR_H

#include "esp_err.h"

/* Product-specific post-boot qualification. This is intentionally separate
 * from the generic rollback mechanics and has no OTA source/policy role. */
esp_err_t ota_boot_validator_start_normal(void);
esp_err_t ota_boot_validator_start_recovery(void);

/* Called by controlled local startup-failure paths. It requests rollback only
 * for the image currently running PENDING_VERIFY. */
esp_err_t ota_boot_validator_handle_local_failure(const char *stage);

#endif
