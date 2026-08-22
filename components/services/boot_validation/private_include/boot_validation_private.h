#ifndef BOOT_VALIDATION_PRIVATE_H
#define BOOT_VALIDATION_PRIVATE_H

#include "boot_validation.h"
#include "platform_ota.h"

typedef struct {
    esp_err_t (*get_running_partition)(platform_ota_partition_t *out);
    esp_err_t (*get_partition_state)(const platform_ota_partition_t *partition,
                                     platform_ota_image_state_t *out);
    esp_err_t (*mark_running_valid)(void);
    esp_err_t (*mark_running_invalid_and_rollback_reboot)(void);
} boot_validation_ops_t;

void boot_validation_test_set_ops(const boot_validation_ops_t *ops);
void boot_validation_test_reset(void);
#endif
