#include <string.h>

#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "ota_service_private.h"

enum {
    OTA_PREFIX_SIZE = sizeof(esp_image_header_t) +
                      sizeof(esp_image_segment_header_t) +
                      sizeof(esp_app_desc_t)
};

esp_err_t ota_validator_validate_prefix(const uint8_t *prefix, size_t size,
                                        const esp_app_desc_t *running,
                                        ota_image_info_t *out_image)
{
    if (prefix == NULL || out_image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (size < OTA_PREFIX_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (running == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_image_header_t *header = (const esp_image_header_t *)prefix;
    const esp_app_desc_t *desc = (const esp_app_desc_t *)(prefix + sizeof(*header) +
                                                          sizeof(esp_image_segment_header_t));

    if (header->magic != ESP_IMAGE_HEADER_MAGIC ||
        header->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID ||
        desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }

    if (running->project_name[0] == '\0' ||
        memchr(running->project_name, '\0', sizeof(running->project_name)) == NULL ||
        desc->project_name[0] == '\0' ||
        memchr(desc->project_name, '\0', sizeof(desc->project_name)) == NULL ||
        strcmp(desc->project_name, running->project_name) != 0 ||
        desc->version[0] == '\0' ||
        memchr(desc->version, '\0', sizeof(desc->version)) == NULL) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }

    memset(out_image, 0, sizeof(*out_image));
    memcpy(out_image->project_name, desc->project_name, sizeof(out_image->project_name) - 1U);
    memcpy(out_image->version, desc->version, sizeof(out_image->version) - 1U);
    out_image->secure_version = desc->secure_version;
    return ESP_OK;
}

size_t ota_validator_prefix_size(void)
{
    return OTA_PREFIX_SIZE;
}