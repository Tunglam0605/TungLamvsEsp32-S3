#ifndef OTA_SERVICE_PRIVATE_H
#define OTA_SERVICE_PRIVATE_H

#include "esp_app_desc.h"
#include "ota_service.h"

/* Test seam. Production uses the platform_ota wrapper and esp_app_get_description(). */
typedef struct {
    esp_err_t (*get_next_update_partition)(platform_ota_partition_t *target);
    esp_err_t (*session_begin)(platform_ota_session_t *session,
                               const platform_ota_partition_t *target, size_t image_size);
    esp_err_t (*session_write)(platform_ota_session_t *session, const void *data, size_t size);
    esp_err_t (*session_finish)(platform_ota_session_t *session);
    esp_err_t (*session_abort)(platform_ota_session_t *session);
    bool (*session_is_active)(const platform_ota_session_t *session);
    esp_err_t (*set_boot_partition)(const platform_ota_partition_t *target);
    const esp_app_desc_t *(*get_running_description)(void);
} ota_service_ops_t;

esp_err_t ota_validator_validate_prefix(const uint8_t *prefix, size_t size,
                                        const esp_app_desc_t *running,
                                        ota_image_info_t *out_image);
size_t ota_validator_prefix_size(void);

esp_err_t ota_session_check_chunk_bounds(size_t expected_size,
                                         size_t bytes_received,
                                         size_t chunk_size);
bool ota_session_expected_complete(size_t expected_size, size_t bytes_received);

void ota_service_test_set_ops(const ota_service_ops_t *ops);
void ota_service_test_reset_ops(void);
void ota_service_test_force_reset(void);

#endif /* OTA_SERVICE_PRIVATE_H */