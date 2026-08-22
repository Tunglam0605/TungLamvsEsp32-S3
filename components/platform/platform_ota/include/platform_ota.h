#ifndef PLATFORM_OTA_H
#define PLATFORM_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_OTA_PARTITION_LABEL_MAX 16U
#define PLATFORM_OTA_IMAGE_SIZE_UNKNOWN ((size_t)-1)
#define PLATFORM_OTA_SESSION_INITIALIZER {0, NULL, 0, 0, 0, 0x504F5441U, 0}

typedef enum {
    PLATFORM_OTA_SLOT_UNKNOWN = 0,
    PLATFORM_OTA_SLOT_FACTORY,
    PLATFORM_OTA_SLOT_0,
    PLATFORM_OTA_SLOT_1,
    PLATFORM_OTA_SLOT_OTHER,
} platform_ota_slot_t;

typedef enum {
    PLATFORM_OTA_IMG_UNDEFINED = 0,
    PLATFORM_OTA_IMG_NEW,
    PLATFORM_OTA_IMG_PENDING_VERIFY,
    PLATFORM_OTA_IMG_VALID,
    PLATFORM_OTA_IMG_INVALID,
    PLATFORM_OTA_IMG_ABORTED,
} platform_ota_image_state_t;

typedef struct {
    char label[PLATFORM_OTA_PARTITION_LABEL_MAX + 1U];
    uint32_t address;
    size_t size;
    platform_ota_slot_t slot;
    const void *_native;
} platform_ota_partition_t;

typedef struct {
    uintptr_t _handle;
    const void *_target_native;
    size_t _target_size;
    size_t _expected_size;
    size_t _bytes_written;
    uint32_t _magic;
    uint8_t _active;
} platform_ota_session_t;

void platform_ota_session_init(platform_ota_session_t *session);
esp_err_t platform_ota_get_running_partition(platform_ota_partition_t *out_partition);
esp_err_t platform_ota_get_next_update_partition(platform_ota_partition_t *out_partition);
esp_err_t platform_ota_session_begin(platform_ota_session_t *session,
                                     const platform_ota_partition_t *target,
                                     size_t image_size);
esp_err_t platform_ota_session_write(platform_ota_session_t *session,
                                     const void *data,
                                     size_t size);
esp_err_t platform_ota_session_finish(platform_ota_session_t *session);
esp_err_t platform_ota_session_abort(platform_ota_session_t *session);
esp_err_t platform_ota_set_boot_partition(const platform_ota_partition_t *target);
esp_err_t platform_ota_get_partition_state(const platform_ota_partition_t *partition,
                                           platform_ota_image_state_t *out_state);
esp_err_t platform_ota_mark_running_valid(void);
esp_err_t platform_ota_mark_running_invalid_and_rollback_reboot(void);
bool platform_ota_session_is_active(const platform_ota_session_t *session);
size_t platform_ota_session_bytes_written(const platform_ota_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_OTA_H */