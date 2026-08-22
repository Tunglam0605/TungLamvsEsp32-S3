#ifndef BOOT_VALIDATION_H
#define BOOT_VALIDATION_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_VALIDATION_NOT_PENDING = 0,
    BOOT_VALIDATION_PENDING,
    BOOT_VALIDATION_VALID,
    BOOT_VALIDATION_ROLLBACK_REQUESTED,
} boot_validation_lifecycle_t;

/* Inspect the currently running OTA partition once per boot.  A factory image
 * or a valid/non-OTA image is deliberately a no-op. */
esp_err_t boot_validation_init(void);
bool boot_validation_is_pending(void);
boot_validation_lifecycle_t boot_validation_get_lifecycle(void);

/* These affect the running image only when this boot began PENDING_VERIFY. */
esp_err_t boot_validation_mark_valid(void);
esp_err_t boot_validation_request_rollback(void);

#ifdef __cplusplus
}
#endif
#endif
