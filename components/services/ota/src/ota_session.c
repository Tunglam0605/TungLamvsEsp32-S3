#include "ota_service_private.h"

esp_err_t ota_session_check_chunk_bounds(size_t expected_size,
                                         size_t bytes_received,
                                         size_t chunk_size)
{
    if (expected_size == OTA_IMAGE_SIZE_UNKNOWN) {
        return ESP_OK;
    }
    if (bytes_received > expected_size || chunk_size > expected_size - bytes_received) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

bool ota_session_expected_complete(size_t expected_size, size_t bytes_received)
{
    return expected_size == OTA_IMAGE_SIZE_UNKNOWN || bytes_received == expected_size;
}