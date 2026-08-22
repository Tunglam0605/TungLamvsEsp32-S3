#ifndef PLATFORM_OTA_INTERNAL_H
#define PLATFORM_OTA_INTERNAL_H

#include <stddef.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"

typedef struct {
    const esp_partition_t *(*get_running_partition)(void);
    const esp_partition_t *(*get_next_update_partition)(const esp_partition_t *start_from);
    esp_err_t (*begin)(const esp_partition_t *partition, size_t image_size,
                       esp_ota_handle_t *out_handle);
    esp_err_t (*write)(esp_ota_handle_t handle, const void *data, size_t size);
    esp_err_t (*end)(esp_ota_handle_t handle);
    esp_err_t (*abort)(esp_ota_handle_t handle);
    esp_err_t (*set_boot_partition)(const esp_partition_t *partition);
    esp_err_t (*get_state_partition)(const esp_partition_t *partition,
                                     esp_ota_img_states_t *state);
    esp_err_t (*mark_app_valid_cancel_rollback)(void);
    esp_err_t (*mark_app_invalid_rollback_and_reboot)(void);
} platform_ota_ops_t;

extern const platform_ota_ops_t g_platform_ota_idf_ops;

/* Private test seam. Never use from product/service code. */
void platform_ota_test_set_ops(const platform_ota_ops_t *ops);
void platform_ota_test_reset_ops(void);

#endif /* PLATFORM_OTA_INTERNAL_H */