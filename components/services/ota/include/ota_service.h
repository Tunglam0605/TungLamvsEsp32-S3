#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include "ota_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_IMAGE_SIZE_UNKNOWN PLATFORM_OTA_IMAGE_SIZE_UNKNOWN

/** Initialise the process-wide OTA service. Idempotent only while the service is IDLE. */
esp_err_t ota_service_init(void);

/**
 * Reset a transaction to IDLE, aborting an active platform session if needed.
 * Reset is rejected after boot selection has entered INSTALLING/REBOOT_PENDING.
 */
esp_err_t ota_service_reset(void);

/** Admit one image stream. No target erase/write happens until its prefix validates. */
esp_err_t ota_service_begin(size_t expected_image_size);

/** Stream one chunk. A zero-length chunk is a safe no-op while receiving a transaction. */
esp_err_t ota_service_write(const void *data, size_t size);
esp_err_t ota_service_finish(void);
esp_err_t ota_service_abort(void);

/** Forget a staged image or recorded failure; this never activates an image. */
esp_err_t ota_service_discard(void);

/** Set the staged image as the boot partition. This function deliberately never reboots. */
esp_err_t ota_service_install(void);

esp_err_t ota_service_get_status(ota_status_t *out_status);

/**
 * Register a synchronous event callback. Events carry immutable status snapshots.
 * The callback is invoked after the OTA service mutex is released and may safely query the service.
 */
esp_err_t ota_service_set_event_callback(ota_event_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif

#endif /* OTA_SERVICE_H */