#ifndef OTA_TYPES_H
#define OTA_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_ADMISSION,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFYING,
    OTA_STATE_STAGED,
    OTA_STATE_INSTALLING,
    OTA_STATE_REBOOT_PENDING,
    OTA_STATE_FAILED,
} ota_state_t;

typedef struct {
    char project_name[32];
    char version[32];
    uint32_t size;
    uint32_t secure_version;
} ota_image_info_t;

typedef struct {
    ota_state_t state;
    esp_err_t last_error;
    size_t expected_size;
    size_t bytes_received;
    bool platform_session_active;
    bool has_staged_image;
    platform_ota_partition_t target_partition;
    ota_image_info_t image;
} ota_status_t;

#ifdef __cplusplus
}
#endif

#endif /* OTA_TYPES_H */
