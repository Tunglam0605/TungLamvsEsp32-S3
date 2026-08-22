#include "ota_web_handler.h"

#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

#include "config_portal_private.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_policy.h"
#include "ota_service.h"

static const char *TAG = "OTA_WEB";
#define OTA_HTTP_CHUNK_SIZE 4096U
#define OTA_HTTP_MAX_CONSECUTIVE_TIMEOUTS 3U

/* The HTTP daemon processes requests serially. Keep the streaming buffer out
 * of its task stack, while still passing every 4096-byte receive straight to
 * the generic OTA service. */
static uint8_t s_upload_chunk[OTA_HTTP_CHUNK_SIZE];

static const char *state_name(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_IDLE: return "idle";
    case OTA_STATE_ADMISSION: return "admission";
    case OTA_STATE_RECEIVING: return "receiving";
    case OTA_STATE_VERIFYING: return "verifying";
    case OTA_STATE_STAGED: return "staged";
    case OTA_STATE_INSTALLING: return "installing";
    case OTA_STATE_REBOOT_PENDING: return "reboot_pending";
    case OTA_STATE_FAILED: return "failed";
    default: return "unknown";
    }
}

static bool require_auth(httpd_req_t *req)
{
    if (config_portal_request_is_authorized(req)) return true;
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Login required");
    return false;
}

static esp_err_t send_error_with_status(httpd_req_t *req, const char *status,
                                        const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, message);
}

static bool request_has_binary_content_type(httpd_req_t *req)
{
    static const char media_type[] = "application/octet-stream";
    char content_type[64];
    const size_t header_length = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (header_length == 0U || header_length >= sizeof(content_type) ||
        httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
                                   sizeof(content_type)) != ESP_OK) {
        return false;
    }

    const char *value = content_type;
    while (isspace((unsigned char)*value)) ++value;
    const size_t media_length = sizeof(media_type) - 1U;
    if (strncasecmp(value, media_type, media_length) != 0) return false;
    value += media_length;
    return *value == '\0' || *value == ';' || isspace((unsigned char)*value);
}

static void json_escape(const char *source, char *destination, size_t destination_size)
{
    size_t written = 0;
    if (destination_size == 0U) return;
    for (; source && *source; ++source) {
        const unsigned char character = (unsigned char)*source;
        const char *escape = NULL;
        char unicode_escape[7];
        if (character == '"') escape = "\\\"";
        else if (character == '\\') escape = "\\\\";
        else if (character == '\b') escape = "\\b";
        else if (character == '\f') escape = "\\f";
        else if (character == '\n') escape = "\\n";
        else if (character == '\r') escape = "\\r";
        else if (character == '\t') escape = "\\t";
        else if (character < 0x20U) {
            (void)snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", character);
            escape = unicode_escape;
        }
        if (escape) {
            const size_t escape_length = strlen(escape);
            if (written + escape_length >= destination_size) break;
            memcpy(destination + written, escape, escape_length);
            written += escape_length;
        } else {
            if (written + 1U >= destination_size) break;
            destination[written++] = (char)character;
        }
    }
    destination[written] = '\0';
}

static esp_err_t send_policy_denied(httpd_req_t *req, const ota_policy_decision_t *decision)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"ok\":false,\"reason\":\"%s\"}",
             ota_policy_reason_name(decision->reason));
    httpd_resp_set_status(req, decision->reason == OTA_POLICY_DENY_UNAUTHORIZED ?
                              "401 Unauthorized" : "409 Conflict");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t check_policy(httpd_req_t *req, ota_policy_action_t action)
{
    ota_policy_decision_t decision;
    const esp_err_t err = ota_policy_evaluate(OTA_POLICY_SOURCE_WEB_LOCAL, action,
                                              true, &decision);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA policy unavailable");
        return err;
    }
    if (!decision.allowed) {
        (void)send_policy_denied(req, &decision);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t ota_status_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    ota_status_t status;
    const esp_err_t err = ota_service_get_status(&status);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA unavailable");
    char target[PLATFORM_OTA_PARTITION_LABEL_MAX * 6U + 1U];
    char version[sizeof(status.image.version) * 6U + 1U];
    char project[sizeof(status.image.project_name) * 6U + 1U];
    json_escape(status.target_partition.label, target, sizeof(target));
    json_escape(status.image.version, version, sizeof(version));
    json_escape(status.image.project_name, project, sizeof(project));
    char body[640];
    const unsigned progress = status.expected_size != OTA_IMAGE_SIZE_UNKNOWN && status.expected_size > 0U ?
        (unsigned)((status.bytes_received / status.expected_size) * 100U +
                   ((status.bytes_received % status.expected_size) * 100U) /
                       status.expected_size) : 0U;
    snprintf(body, sizeof(body),
             "{\"state\":\"%s\",\"error\":%ld,\"expected\":%lu,\"received\":%lu,"
             "\"progress\":%u,\"staged\":%s,\"target\":\"%s\",\"version\":\"%s\","
             "\"project\":\"%s\",\"image_size\":%lu}",
             state_name(status.state), (long)status.last_error,
             (unsigned long)(status.expected_size == OTA_IMAGE_SIZE_UNKNOWN ? 0U : status.expected_size),
             (unsigned long)status.bytes_received, progress,
             status.has_staged_image ? "true" : "false", target, version, project,
             (unsigned long)status.image.size);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    if (!request_has_binary_content_type(req)) {
        return send_error_with_status(req, "415 Unsupported Media Type",
                                      "Content-Type must be application/octet-stream");
    }
    if (req->content_len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty firmware image");
    }
    if (check_policy(req, OTA_POLICY_ACTION_BEGIN) != ESP_OK) return ESP_OK;

    config_portal_set_ota_activity(true);
    const size_t content_length = (size_t)req->content_len;
    esp_err_t err = ota_service_begin(content_length);
    if (err != ESP_OK) {
        config_portal_set_ota_activity(false);
        return send_error_with_status(req, "409 Conflict", "Cannot begin OTA transaction");
    }

    size_t remaining = content_length;
    unsigned consecutive_timeouts = 0;
    while (remaining > 0U) {
        const size_t wanted = remaining < sizeof(s_upload_chunk) ? remaining : sizeof(s_upload_chunk);
        const int received = httpd_req_recv(req, (char *)s_upload_chunk, wanted);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++consecutive_timeouts <= OTA_HTTP_MAX_CONSECUTIVE_TIMEOUTS) continue;
            ESP_LOGW(TAG, "OTA upload timed out");
            (void)ota_service_abort();
            config_portal_set_ota_activity(false);
            (void)httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Firmware upload timed out");
            return ESP_FAIL;
        }
        if (received <= 0) {
            ESP_LOGW(TAG, "OTA upload disconnected with %lu byte(s) remaining", (unsigned long)remaining);
            (void)ota_service_abort();
            config_portal_set_ota_activity(false);
            return ESP_FAIL;
        }
        consecutive_timeouts = 0;
        err = ota_service_write(s_upload_chunk, (size_t)received);
        if (err != ESP_OK) {
            (void)ota_service_abort();
            config_portal_set_ota_activity(false);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware stream rejected");
            return ESP_OK;
        }
        remaining -= (size_t)received;
    }

    err = ota_service_finish();
    config_portal_set_ota_activity(false);
    if (err != ESP_OK) {
        (void)ota_service_abort();
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware validation failed");
        return ESP_OK;
    }
    ota_status_t status;
    if (ota_service_get_status(&status) != ESP_OK || status.state != OTA_STATE_STAGED ||
        !status.has_staged_image) {
        config_portal_set_ota_activity(false);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA staging state unavailable");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"state\":\"staged\"}");
}

static esp_err_t ota_discard_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    if (check_policy(req, OTA_POLICY_ACTION_DISCARD) != ESP_OK) return ESP_OK;
    const esp_err_t err = ota_service_discard();
    if (err != ESP_OK) return send_error_with_status(req, "409 Conflict", "Cannot discard OTA image");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"state\":\"idle\"}");
}

static esp_err_t ota_install_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    if (check_policy(req, OTA_POLICY_ACTION_INSTALL) != ESP_OK) return ESP_OK;
    const esp_err_t err = ota_service_install();
    if (err != ESP_OK) return send_error_with_status(req, "409 Conflict", "Cannot install OTA image");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    (void)httpd_resp_sendstr(req, "{\"ok\":true,\"state\":\"reboot_pending\"}");
    /* Product boundary owns restart. Generic ota_service_install() only selects
     * the boot partition so upload success can never imply activation. */
    vTaskDelay(pdMS_TO_TICKS(350));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_page_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    static const char page[] =
        "<!doctype html><html lang='vi'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>CallBox OTA</title><style>body{margin:0;background:#0d1525;color:#f8fafc;font:15px system-ui}main{max-width:760px;margin:auto;padding:24px}.c{background:#172236;border:1px solid #3b4b64;border-radius:12px;padding:20px;margin:14px 0}button,input{font:inherit}button{padding:11px 16px;border:1px solid #34d399;border-radius:8px;background:#047857;color:white;margin:6px 6px 6px 0}button:disabled{opacity:.5}.bar{height:12px;background:#111a2c;border-radius:8px;overflow:hidden}.bar i{display:block;height:100%;width:0;background:#34d399}.muted{color:#a9b7ca}code{color:#39cdf8}</style></head>"
        "<body><main><p><a href='/' style='color:#39cdf8'>&larr; CallBox</a></p><h1>Firmware OTA</h1><div class='c'><input id='f' type='file' accept='.bin,application/octet-stream'><br><button id='up'>Upload & Verify</button><button id='install'>Install & Reboot</button><button id='discard'>Discard</button><div class='bar'><i id='bar'></i></div><p id='msg' class='muted'>Reading status...</p><pre id='st'></pre></div>"
        "<script>const q=s=>document.querySelector(s),st=q('#st'),msg=q('#msg'),bar=q('#bar');let busy=false;async function refresh(){if(busy)return;try{let j=await(await fetch('/api/ota/status',{cache:'no-store'})).json();st.textContent=JSON.stringify(j,null,2);bar.style.width=(j.progress||0)+'%';q('#install').disabled=j.state!=='staged';q('#discard').disabled=!['staged','failed'].includes(j.state)}catch(e){msg.textContent='Status unavailable'}}setInterval(refresh,1000);refresh();q('#up').onclick=()=>{let f=q('#f').files[0];if(!f)return;busy=true;let x=new XMLHttpRequest();x.open('POST','/api/ota/upload');x.setRequestHeader('Content-Type','application/octet-stream');x.upload.onprogress=e=>{if(e.lengthComputable)bar.style.width=Math.round(e.loaded*100/e.total)+'%'};x.onload=()=>{busy=false;msg.textContent=x.status+' '+x.responseText;refresh()};x.onerror=()=>{busy=false;msg.textContent='Upload disconnected';refresh()};x.send(f)};q('#install').onclick=async()=>{let r=await fetch('/api/ota/install',{method:'POST'});msg.textContent=await r.text()};q('#discard').onclick=async()=>{let r=await fetch('/api/ota/discard',{method:'POST'});msg.textContent=await r.text();refresh()}</script></main></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ota_web_handler_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t routes[] = {
        { .uri = "/ota", .method = HTTP_GET, .handler = ota_page_handler },
        { .uri = "/api/ota/status", .method = HTTP_GET, .handler = ota_status_handler },
        { .uri = "/api/ota/upload", .method = HTTP_POST, .handler = ota_upload_handler },
        { .uri = "/api/ota/install", .method = HTTP_POST, .handler = ota_install_handler },
        { .uri = "/api/ota/discard", .method = HTTP_POST, .handler = ota_discard_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
