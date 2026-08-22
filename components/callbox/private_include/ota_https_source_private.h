#ifndef CALLBOX_OTA_HTTPS_SOURCE_PRIVATE_H
#define CALLBOX_OTA_HTTPS_SOURCE_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define OTA_HTTPS_MANIFEST_MAX_BYTES 1024U
#define OTA_HTTPS_URL_MAX_LEN 256U
#define OTA_HTTPS_PROJECT_MAX_LEN 32U
#define OTA_HTTPS_VERSION_MAX_LEN 32U

typedef struct {
    char firmware_url[OTA_HTTPS_URL_MAX_LEN];
    char project[OTA_HTTPS_PROJECT_MAX_LEN];
    char version[OTA_HTTPS_VERSION_MAX_LEN];
    bool has_size;
    size_t size;
} ota_https_manifest_t;

/* Pure parser seam: no network, OTA service, task, or ESP-IDF HTTP calls. */
esp_err_t ota_https_manifest_parse(const char *json, size_t json_len,
                                   ota_https_manifest_t *out_manifest);
bool ota_https_url_is_secure(const char *url);
bool ota_https_manifest_matches_project(const ota_https_manifest_t *manifest,
                                        const char *project_name);

#endif /* CALLBOX_OTA_HTTPS_SOURCE_PRIVATE_H */
