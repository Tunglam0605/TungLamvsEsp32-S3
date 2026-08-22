#include "ota_https_source.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ota_https_source_private.h"
#include "ota_policy.h"
#include "ota_service.h"

#ifndef CONFIG_CALLBOX_OTA_HTTPS_ENABLE
#define CONFIG_CALLBOX_OTA_HTTPS_ENABLE 0
#endif
#ifndef CONFIG_CALLBOX_OTA_HTTPS_MANIFEST_URL
#define CONFIG_CALLBOX_OTA_HTTPS_MANIFEST_URL ""
#endif

#define OTA_HTTPS_IMAGE_CHUNK_BYTES 4096U
#define OTA_HTTPS_TASK_STACK_BYTES 6144U

static const char *TAG = "OTA_HTTPS";
static SemaphoreHandle_t s_lock;
static bool s_worker_running;
/* One process-wide worker exists, so a bounded static transfer buffer avoids
 * consuming most of the worker task stack while preserving streaming semantics. */
static uint8_t s_image_chunk[OTA_HTTPS_IMAGE_CHUNK_BYTES];

static void skip_ws(const char **p, const char *end)
{
    while (*p < end && isspace((unsigned char)**p)) ++*p;
}

static bool take_char(const char **p, const char *end, char expected)
{
    skip_ws(p, end);
    if (*p == end || **p != expected) return false;
    ++*p;
    return true;
}

/* OTA URL/project/version fields intentionally accept only plain printable
 * JSON strings. Escaped strings are unnecessary for this wire contract and
 * rejecting them keeps URLs and manifest identity unambiguous. */
static bool take_plain_string(const char **p, const char *end, char *out, size_t out_size)
{
    skip_ws(p, end);
    if (*p == end || **p != '"' || out_size == 0U) return false;
    ++*p;
    size_t written = 0;
    while (*p < end && **p != '"') {
        const unsigned char c = (unsigned char)**p;
        if (c < 0x20U || c == '\\' || written + 1U >= out_size) return false;
        out[written++] = (char)c;
        ++*p;
    }
    if (*p == end || **p != '"' || written == 0U) return false;
    out[written] = '\0';
    ++*p;
    return true;
}

static bool take_size(const char **p, const char *end, size_t *out)
{
    skip_ws(p, end);
    if (*p == end || !isdigit((unsigned char)**p)) return false;
    size_t value = 0;
    do {
        const unsigned digit = (unsigned)(**p - '0');
        if (value > (SIZE_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        ++*p;
    } while (*p < end && isdigit((unsigned char)**p));
    if (value == 0U) return false;
    *out = value;
    return true;
}

bool ota_https_url_is_secure(const char *url)
{
    static const char scheme[] = "https://";
    if (!url || strncmp(url, scheme, sizeof(scheme) - 1U) != 0 ||
        url[sizeof(scheme) - 1U] == '\0') return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) {
        if (*p <= 0x20U || *p == 0x7fU) return false;
    }
    return true;
}

bool ota_https_manifest_matches_project(const ota_https_manifest_t *manifest,
                                        const char *project_name)
{
    return manifest && project_name && project_name[0] != '\0' &&
           strcmp(manifest->project, project_name) == 0;
}

esp_err_t ota_https_manifest_parse(const char *json, size_t json_len,
                                   ota_https_manifest_t *out_manifest)
{
    if (!json || !out_manifest || json_len == 0U || json_len > OTA_HTTPS_MANIFEST_MAX_BYTES)
        return ESP_ERR_INVALID_ARG;
    memset(out_manifest, 0, sizeof(*out_manifest));
    const char *p = json;
    const char *const end = json + json_len;
    bool got_url = false, got_project = false, got_version = false, got_size = false;
    if (!take_char(&p, end, '{')) return ESP_ERR_INVALID_RESPONSE;
    skip_ws(&p, end);
    if (p == end || *p == '}') return ESP_ERR_INVALID_RESPONSE;
    while (p < end) {
        char key[16];
        if (!take_plain_string(&p, end, key, sizeof(key)) || !take_char(&p, end, ':'))
            return ESP_ERR_INVALID_RESPONSE;
        if (strcmp(key, "firmware_url") == 0) {
            if (got_url || !take_plain_string(&p, end, out_manifest->firmware_url,
                                               sizeof(out_manifest->firmware_url)))
                return ESP_ERR_INVALID_RESPONSE;
            got_url = true;
        } else if (strcmp(key, "project") == 0) {
            if (got_project || !take_plain_string(&p, end, out_manifest->project,
                                                   sizeof(out_manifest->project)))
                return ESP_ERR_INVALID_RESPONSE;
            got_project = true;
        } else if (strcmp(key, "version") == 0) {
            if (got_version || !take_plain_string(&p, end, out_manifest->version,
                                                   sizeof(out_manifest->version)))
                return ESP_ERR_INVALID_RESPONSE;
            got_version = true;
        } else if (strcmp(key, "size") == 0) {
            if (got_size || !take_size(&p, end, &out_manifest->size)) return ESP_ERR_INVALID_RESPONSE;
            got_size = true;
            out_manifest->has_size = true;
        } else {
            return ESP_ERR_INVALID_RESPONSE;
        }
        skip_ws(&p, end);
        if (p < end && *p == '}') {
            ++p;
            break;
        }
        if (!take_char(&p, end, ',')) return ESP_ERR_INVALID_RESPONSE;
    }
    skip_ws(&p, end);
    if (p != end || !got_url || !got_project || !got_version ||
        !ota_https_url_is_secure(out_manifest->firmware_url)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

static esp_err_t open_https(const char *url, esp_http_client_handle_t *out_client, int64_t *out_length)
{
    if (!ota_https_url_is_secure(url) || !out_client || !out_length) return ESP_ERR_INVALID_ARG;
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = false,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        const int64_t length = esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client) != 200 || length < 0) err = ESP_ERR_INVALID_RESPONSE;
        else *out_length = length;
    }
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    *out_client = client;
    return ESP_OK;
}

static esp_err_t read_manifest(const char *url, ota_https_manifest_t *manifest)
{
    esp_http_client_handle_t client = NULL;
    int64_t content_length = 0;
    esp_err_t err = open_https(url, &client, &content_length);
    if (err != ESP_OK) return err;
    if (content_length <= 0 || content_length > OTA_HTTPS_MANIFEST_MAX_BYTES) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }
    char body[OTA_HTTPS_MANIFEST_MAX_BYTES + 1U];
    size_t received = 0;
    while (received < (size_t)content_length) {
        const int read = esp_http_client_read(client, body + received,
                                               (int)((size_t)content_length - received));
        if (read <= 0) { err = ESP_ERR_INVALID_RESPONSE; break; }
        received += (size_t)read;
    }
    esp_http_client_cleanup(client);
    if (err != ESP_OK) return err;
    body[received] = '\0';
    return ota_https_manifest_parse(body, received, manifest);
}

static esp_err_t stream_image(const ota_https_manifest_t *manifest)
{
    esp_http_client_handle_t client = NULL;
    int64_t content_length = 0;
    esp_err_t err = open_https(manifest->firmware_url, &client, &content_length);
    if (err != ESP_OK) return err;
    if (manifest->has_size && ((uint64_t)content_length != (uint64_t)manifest->size)) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }
    err = ota_service_begin(manifest->has_size ? manifest->size : (size_t)content_length);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    while (err == ESP_OK) {
        const int read = esp_http_client_read(client, (char *)s_image_chunk, sizeof(s_image_chunk));
        if (read < 0) { err = ESP_FAIL; break; }
        if (read == 0) break;
        err = ota_service_write(s_image_chunk, (size_t)read);
    }
    esp_http_client_cleanup(client);
    if (err == ESP_OK) err = ota_service_finish();
    if (err == ESP_OK) {
        ota_status_t status;
        if (ota_service_get_status(&status) != ESP_OK || status.state != OTA_STATE_STAGED ||
            !status.has_staged_image ||
            !ota_https_manifest_matches_project(manifest, status.image.project_name) ||
            strcmp(manifest->version, status.image.version) != 0) {
            (void)ota_service_discard();
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (err != ESP_OK) (void)ota_service_abort();
    return err;
}

static void ota_https_worker(void *unused)
{
    (void)unused;
    ota_policy_decision_t decision;
    esp_err_t err = ota_policy_evaluate(OTA_POLICY_SOURCE_INTERNAL_TRIGGER,
                                        OTA_POLICY_ACTION_BEGIN, false, &decision);
    if (err == ESP_OK && !decision.allowed) err = ESP_ERR_INVALID_STATE;
    ota_https_manifest_t manifest;
    if (err == ESP_OK) err = read_manifest(CONFIG_CALLBOX_OTA_HTTPS_MANIFEST_URL, &manifest);
    if (err == ESP_OK && !ota_https_manifest_matches_project(
                             &manifest, esp_app_get_description()->project_name))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = stream_image(&manifest);
    if (err == ESP_OK) ESP_LOGI(TAG, "HTTPS OTA staged version %s", manifest.version);
    else ESP_LOGW(TAG, "HTTPS OTA request failed: %s", esp_err_to_name(err));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_worker_running = false;
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

esp_err_t ota_https_source_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t ota_https_source_request_configured(void)
{
    if (!CONFIG_CALLBOX_OTA_HTTPS_ENABLE) return ESP_ERR_NOT_SUPPORTED;
    if (!ota_https_url_is_secure(CONFIG_CALLBOX_OTA_HTTPS_MANIFEST_URL)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ota_https_source_init();
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_worker_running) err = ESP_ERR_INVALID_STATE;
    else {
        /* Mark before create: a higher-priority worker is allowed to run as
         * soon as xTaskCreate returns. */
        s_worker_running = true;
        if (xTaskCreate(ota_https_worker, "ota_https", OTA_HTTPS_TASK_STACK_BYTES,
                        NULL, 4, NULL) != pdPASS) {
            s_worker_running = false;
            err = ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}
