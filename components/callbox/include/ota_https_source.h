#ifndef CALLBOX_OTA_HTTPS_SOURCE_H
#define CALLBOX_OTA_HTTPS_SOURCE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the asynchronous HTTPS OTA source. Safe to call once at boot. */
esp_err_t ota_https_source_init(void);

/**
 * Queue a non-blocking download from CONFIG_CALLBOX_OTA_HTTPS_MANIFEST_URL.
 * The caller returns after the worker has been created. The worker applies
 * normal-mode internal-trigger policy before any manifest or image request.
 * A successful request only leaves the generic OTA service in STAGED.
 */
esp_err_t ota_https_source_request_configured(void);

#ifdef __cplusplus
}
#endif

#endif /* CALLBOX_OTA_HTTPS_SOURCE_H */
