#include "gateway_auth.h"
#include "gateway_web_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "platform_nvs.h"

#define AUTH_NS "gateway_auth"
#define ACCOUNT_COUNT 4U
#define SESSION_COUNT 4U
#define SALT_BYTES 16U
#define HASH_BYTES 32U
#define PBKDF2_ITERATIONS 12000U
#define SESSION_LIFETIME_MS (30LL * 60LL * 1000LL)
#define LOGIN_SOURCE_COUNT 6U
#define LOGIN_FAILURE_LIMIT 5U
#define LOGIN_WINDOW_MS 60000LL
#define LOGIN_BLOCK_MS 60000LL

typedef struct {
    char username[24];
    gateway_role_t role;
    bool enabled;
    uint8_t salt[SALT_BYTES];
    uint8_t hash[HASH_BYTES];
} account_t;

typedef struct {
    bool active;
    char token[65];
    gateway_auth_session_t public;
} session_t;

static account_t s_accounts[ACCOUNT_COUNT];
static session_t s_sessions[SESSION_COUNT];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_ready;
typedef struct {
    bool used;
    uint32_t source_ipv4;
    uint8_t failures;
    int64_t window_started_ms;
    int64_t blocked_until_ms;
    int64_t last_seen_ms;
} login_source_t;
static login_source_t s_login_sources[LOGIN_SOURCE_COUNT];

static const char *const DEFAULT_USERS[ACCOUNT_COUNT] = {
    "admin_factory", "admin_tech", "admin_it", "admin_aubot"
};
static const char *const DEFAULT_PASSWORDS[ACCOUNT_COUNT] = {
    "aubot_factory", "aubot_tech", "aubot_it", "admin_aubot"
};
static const gateway_role_t DEFAULT_ROLES[ACCOUNT_COUNT] = {
    GW_ROLE_FACTORY, GW_ROLE_TECH, GW_ROLE_IT, GW_ROLE_SUPER_ADMIN
};

static void hex_encode(const uint8_t *input, size_t length, char *output)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        output[i * 2U] = HEX[input[i] >> 4U];
        output[i * 2U + 1U] = HEX[input[i] & 15U];
    }
    output[length * 2U] = '\0';
}

static int nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool hex_decode(const char *input, uint8_t *output, size_t length)
{
    if (strlen(input) != length * 2U) return false;
    for (size_t i = 0; i < length; ++i) {
        const int high = nibble(input[i * 2U]);
        const int low = nibble(input[i * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool derive(const char *password, const uint8_t salt[SALT_BYTES],
                   uint8_t output[HASH_BYTES])
{
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        (const unsigned char *)password, strlen(password), salt, SALT_BYTES,
        PBKDF2_ITERATIONS, HASH_BYTES, output) == 0;
}

static bool constant_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
    uint8_t difference = 0U;
    for (size_t i = 0; i < length; ++i) difference |= left[i] ^ right[i];
    return difference == 0U;
}

static account_t *account_find(const char *username)
{
    for (size_t i = 0; i < ACCOUNT_COUNT; ++i)
        if (strcmp(s_accounts[i].username, username) == 0) return &s_accounts[i];
    return NULL;
}

static esp_err_t save_account(size_t index)
{
    platform_nvs_handle_t handle = {0};
    esp_err_t error = platform_nvs_open(&handle, AUTH_NS, false);
    if (error != ESP_OK) return error;
    char key[8], salt[33], hash[65], value[180];
    hex_encode(s_accounts[index].salt, SALT_BYTES, salt);
    hex_encode(s_accounts[index].hash, HASH_BYTES, hash);
    snprintf(key, sizeof(key), "user%u", (unsigned)index);
    snprintf(value, sizeof(value), "%s|%u|%u|%s|%s", s_accounts[index].username,
             (unsigned)s_accounts[index].role, s_accounts[index].enabled ? 1U : 0U,
             salt, hash);
    error = platform_nvs_set_string(&handle, key, value);
    if (error == ESP_OK) error = platform_nvs_set_u8(&handle, "schema", 1U);
    if (error == ESP_OK) error = platform_nvs_commit(&handle);
    platform_nvs_close(&handle);
    return error;
}

static esp_err_t set_default(size_t index)
{
    account_t *account = &s_accounts[index];
    memset(account, 0, sizeof(*account));
    strlcpy(account->username, DEFAULT_USERS[index], sizeof(account->username));
    account->role = DEFAULT_ROLES[index];
    account->enabled = true;
    esp_fill_random(account->salt, sizeof(account->salt));
    if (!derive(DEFAULT_PASSWORDS[index], account->salt, account->hash)) return ESP_FAIL;
    return save_account(index);
}

esp_err_t gateway_auth_restore_defaults(void)
{
    for (size_t i = 0; i < ACCOUNT_COUNT; ++i) {
        const esp_err_t error = set_default(i);
        if (error != ESP_OK) return error;
    }
    taskENTER_CRITICAL(&s_mux);
    memset(s_sessions, 0, sizeof(s_sessions));
    s_ready = true;
    taskEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

esp_err_t gateway_auth_init(void)
{
    s_ready = false;
    platform_nvs_handle_t handle = {0};
    esp_err_t error = platform_nvs_open(&handle, AUTH_NS, true);
    if (error == ESP_ERR_NOT_FOUND) return gateway_auth_restore_defaults();
    if (error != ESP_OK) return error;
    uint8_t schema = 0U;
    bool found = false;
    error = platform_nvs_get_u8(&handle, "schema", &schema, &found);
    if (error != ESP_OK || !found || schema != 1U) {
        platform_nvs_close(&handle);
        return error == ESP_OK ? ESP_ERR_INVALID_VERSION : error;
    }
    for (size_t i = 0; i < ACCOUNT_COUNT; ++i) {
        char key[8], value[180] = {0};
        snprintf(key, sizeof(key), "user%u", (unsigned)i);
        found = false;
        error = platform_nvs_get_string(&handle, key, value, sizeof(value), &found);
        if (error != ESP_OK || !found) break;
        char *save = NULL, *username = strtok_r(value, "|", &save);
        char *role = strtok_r(NULL, "|", &save), *enabled = strtok_r(NULL, "|", &save);
        char *salt = strtok_r(NULL, "|", &save), *hash = strtok_r(NULL, "|", &save);
        if (!username || !role || !enabled || !salt || !hash ||
            strcmp(username, DEFAULT_USERS[i]) != 0 || atoi(role) != DEFAULT_ROLES[i] ||
            !hex_decode(salt, s_accounts[i].salt, SALT_BYTES) ||
            !hex_decode(hash, s_accounts[i].hash, HASH_BYTES)) {
            error = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        strlcpy(s_accounts[i].username, username, sizeof(s_accounts[i].username));
        s_accounts[i].role = (gateway_role_t)atoi(role);
        /* System accounts are fixed and cannot be disabled from WebUI. */
        s_accounts[i].enabled = true;
    }
    platform_nvs_close(&handle);
    if (error == ESP_OK) s_ready = true;
    return error;
}

bool gateway_auth_is_ready(void) { return s_ready; }

bool gateway_auth_role_has_permission(gateway_role_t role, gateway_permission_t permission)
{
    uint32_t permissions = GW_PERMISSION_VIEW_PUBLIC;
    if (role == GW_ROLE_FACTORY)
        permissions |= GW_PERMISSION_WAREHOUSE_CONFIG | GW_PERMISSION_LASER_CONFIG |
                       GW_PERMISSION_NETWORK_CONFIG | GW_PERMISSION_WAREHOUSE_IDENTITY;
    else if (role == GW_ROLE_TECH)
        permissions |= GW_PERMISSION_WAREHOUSE_CONFIG | GW_PERMISSION_LASER_CONFIG |
                       GW_PERMISSION_CAN_DEBUG | GW_PERMISSION_SYSTEM_DEBUG |
                       GW_PERMISSION_NETWORK_CONFIG;
    else if (role == GW_ROLE_IT)
        permissions |= GW_PERMISSION_NETWORK_CONFIG | GW_PERMISSION_MQTT_CONFIG |
                       GW_PERMISSION_ETHERNET_CONFIG;
    else if (role == GW_ROLE_SUPER_ADMIN)
        permissions = 0xffffffffU;
    return role != GW_ROLE_NONE && (permissions & permission) == (uint32_t)permission;
}

const char *gateway_auth_role_name(gateway_role_t role)
{
    switch (role) {
    case GW_ROLE_FACTORY: return "FACTORY";
    case GW_ROLE_TECH: return "TECH";
    case GW_ROLE_IT: return "IT";
    case GW_ROLE_SUPER_ADMIN: return "AUBOT";
    default: return "PUBLIC";
    }
}

static login_source_t *login_source(uint32_t source_ipv4, int64_t now_ms, bool create)
{
    login_source_t *free_slot = NULL, *oldest = &s_login_sources[0];
    for (size_t i = 0; i < LOGIN_SOURCE_COUNT; ++i) {
        login_source_t *slot = &s_login_sources[i];
        if (slot->used && slot->source_ipv4 == source_ipv4) return slot;
        if (!slot->used && free_slot == NULL) free_slot = slot;
        if (slot->last_seen_ms < oldest->last_seen_ms) oldest = slot;
    }
    if (!create) return NULL;
    login_source_t *slot = free_slot != NULL ? free_slot : oldest;
    *slot = (login_source_t){.used=true,.source_ipv4=source_ipv4,
                             .window_started_ms=now_ms,.last_seen_ms=now_ms};
    return slot;
}

bool gateway_auth_login_allowed(uint32_t source_ipv4, int64_t now_ms,
                                uint32_t *retry_after_seconds)
{
    bool allowed = true;
    taskENTER_CRITICAL(&s_mux);
    login_source_t *slot = login_source(source_ipv4, now_ms, false);
    if (slot != NULL) {
        slot->last_seen_ms = now_ms;
        allowed = now_ms >= slot->blocked_until_ms;
        if (!allowed && retry_after_seconds != NULL)
            *retry_after_seconds = (uint32_t)((slot->blocked_until_ms-now_ms+999LL)/1000LL);
    }
    taskEXIT_CRITICAL(&s_mux);
    return allowed;
}

void gateway_auth_login_failed(uint32_t source_ipv4, int64_t now_ms)
{
    taskENTER_CRITICAL(&s_mux);
    login_source_t *slot = login_source(source_ipv4, now_ms, true);
    if (now_ms - slot->window_started_ms > LOGIN_WINDOW_MS) {
        slot->window_started_ms = now_ms;
        slot->failures = 0U;
    }
    slot->last_seen_ms = now_ms;
    if (++slot->failures >= LOGIN_FAILURE_LIMIT) {
        slot->blocked_until_ms = now_ms + LOGIN_BLOCK_MS;
        slot->failures = 0U;
    }
    taskEXIT_CRITICAL(&s_mux);
}

void gateway_auth_login_succeeded(uint32_t source_ipv4)
{
    taskENTER_CRITICAL(&s_mux);
    login_source_t *slot = login_source(source_ipv4, 0, false);
    if (slot != NULL) memset(slot, 0, sizeof(*slot));
    taskEXIT_CRITICAL(&s_mux);
}

bool gateway_auth_authenticate(const char *username, const char *password,
                               gateway_auth_session_t *session, char *token,
                               size_t token_capacity)
{
    const int64_t now = esp_timer_get_time() / 1000LL;
    if (!username || !password || !session || !token || token_capacity < 65U ||
        !s_ready) return false;
    account_t *account = account_find(username);
    uint8_t candidate[HASH_BYTES] = {0};
    const bool derived = account && derive(password, account->salt, candidate);
    if (!account || !account->enabled || !derived ||
        !constant_equal(candidate, account->hash, HASH_BYTES)) {
        return false;
    }
    uint8_t random[32];
    esp_fill_random(random, sizeof(random));
    hex_encode(random, sizeof(random), token);
    gateway_auth_session_t created = {
        .authenticated = true, .role = account->role,
        .expires_at_ms = now + SESSION_LIFETIME_MS,
    };
    strlcpy(created.username, account->username, sizeof(created.username));
    taskENTER_CRITICAL(&s_mux);
    size_t slot = 0U;
    int64_t oldest_expiry = INT64_MAX;
    for (size_t i = 0; i < SESSION_COUNT; ++i) {
        if (!s_sessions[i].active || s_sessions[i].public.expires_at_ms <= now) {
            slot = i;
            oldest_expiry = INT64_MIN;
            break;
        }
        if (s_sessions[i].public.expires_at_ms < oldest_expiry) {
            oldest_expiry = s_sessions[i].public.expires_at_ms;
            slot = i;
        }
    }
    s_sessions[slot].active = true;
    strlcpy(s_sessions[slot].token, token, sizeof(s_sessions[slot].token));
    s_sessions[slot].public = created;
    taskEXIT_CRITICAL(&s_mux);
    *session = created;
    return true;
}

static bool request_token(httpd_req_t *request, char output[65])
{
    char cookie[192];
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK)
        return false;
    char *entry = cookie;
    while (*entry) {
        while (*entry == ' ' || *entry == ';') ++entry;
        char *end = strchr(entry, ';');
        const size_t length = end ? (size_t)(end - entry) : strlen(entry);
        static const char PREFIX[] = "GWSESSION=";
        if (length == sizeof(PREFIX) - 1U + 64U &&
            strncmp(entry, PREFIX, sizeof(PREFIX) - 1U) == 0) {
            memcpy(output, entry + sizeof(PREFIX) - 1U, 64U);
            output[64] = '\0';
            return true;
        }
        if (!end) break;
        entry = end + 1;
    }
    return false;
}

bool gateway_auth_session_from_request(httpd_req_t *request,
                                       gateway_auth_session_t *session)
{
    if (!s_ready) return false;
    char token[65];
    if (!request_token(request, token)) return false;
    const int64_t now = esp_timer_get_time() / 1000LL;
    bool valid = false;
    taskENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < SESSION_COUNT; ++i) {
        if (s_sessions[i].active && s_sessions[i].public.expires_at_ms <= now)
            s_sessions[i].active = false;
        if (s_sessions[i].active && strcmp(s_sessions[i].token, token) == 0) {
            if (session) *session = s_sessions[i].public;
            valid = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_mux);
    return valid;
}

void gateway_auth_logout_request(httpd_req_t *request)
{
    char token[65];
    if (!request_token(request, token)) return;
    taskENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < SESSION_COUNT; ++i)
        if (s_sessions[i].active && strcmp(s_sessions[i].token, token) == 0)
            s_sessions[i].active = false;
    taskEXIT_CRITICAL(&s_mux);
}

bool gateway_auth_require_api(httpd_req_t *request, gateway_permission_t permission,
                              gateway_auth_session_t *session)
{
    if (!s_ready) {
        (void)gateway_web_send_text(request, "503 Service Unavailable",
                                    "Dịch vụ đăng nhập chưa sẵn sàng");
        return false;
    }
    gateway_auth_session_t current;
    if (!gateway_auth_session_from_request(request, &current)) {
        (void)gateway_web_send_text(request, "401 Unauthorized", "Cần đăng nhập");
        return false;
    }
    if (!gateway_auth_role_has_permission(current.role, permission)) {
        (void)gateway_web_send_text(request, "403 Forbidden", "Không đủ quyền");
        return false;
    }
    if (session) *session = current;
    return true;
}

bool gateway_auth_require_page(httpd_req_t *request, gateway_permission_t permission,
                               gateway_auth_session_t *session)
{
    if (!s_ready) {
        (void)gateway_web_send_text(request, "503 Service Unavailable",
                                    "Dịch vụ đăng nhập chưa sẵn sàng");
        return false;
    }
    gateway_auth_session_t current;
    if (!gateway_auth_session_from_request(request, &current)) {
        httpd_resp_set_status(request, "303 See Other");
        httpd_resp_set_hdr(request, "Location", "/login");
        (void)httpd_resp_sendstr(request, "");
        return false;
    }
    if (!gateway_auth_role_has_permission(current.role, permission)) {
        (void)gateway_web_send_text(request, "403 Forbidden", "Không đủ quyền truy cập");
        return false;
    }
    if (session) *session = current;
    return true;
}
