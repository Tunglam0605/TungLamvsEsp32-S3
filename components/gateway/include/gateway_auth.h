#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

typedef enum {
    GW_ROLE_NONE = 0,
    GW_ROLE_FACTORY,
    GW_ROLE_TECH,
    GW_ROLE_IT,
    GW_ROLE_SUPER_ADMIN,
} gateway_role_t;

typedef enum {
    GW_PERMISSION_VIEW_PUBLIC       = 1U << 0,
    GW_PERMISSION_WAREHOUSE_CONFIG = 1U << 1,
    GW_PERMISSION_LASER_CONFIG     = 1U << 2,
    GW_PERMISSION_CAN_DEBUG        = 1U << 3,
    GW_PERMISSION_SYSTEM_DEBUG     = 1U << 4,
    GW_PERMISSION_NETWORK_CONFIG   = 1U << 5,
    GW_PERMISSION_MQTT_CONFIG      = 1U << 6,
    GW_PERMISSION_USER_MANAGEMENT  = 1U << 7,
} gateway_permission_t;

typedef struct {
    bool authenticated;
    char username[24];
    gateway_role_t role;
    int64_t expires_at_ms;
} gateway_auth_session_t;

typedef struct {
    char username[24];
    gateway_role_t role;
    bool enabled;
} gateway_auth_account_info_t;

esp_err_t gateway_auth_init(void);
bool gateway_auth_role_has_permission(gateway_role_t role, gateway_permission_t permission);
const char *gateway_auth_role_name(gateway_role_t role);
bool gateway_auth_login_allowed(int64_t now_ms, uint32_t *retry_after_seconds);
bool gateway_auth_authenticate(const char *username, const char *password,
                               gateway_auth_session_t *session, char *token,
                               size_t token_capacity);
bool gateway_auth_session_from_request(httpd_req_t *request,
                                       gateway_auth_session_t *session);
void gateway_auth_logout_request(httpd_req_t *request);
bool gateway_auth_require_api(httpd_req_t *request, gateway_permission_t permission,
                              gateway_auth_session_t *session);
bool gateway_auth_require_page(httpd_req_t *request, gateway_permission_t permission,
                               gateway_auth_session_t *session);
size_t gateway_auth_accounts(gateway_auth_account_info_t *accounts, size_t capacity);
esp_err_t gateway_auth_set_enabled(const char *username, bool enabled);
esp_err_t gateway_auth_reset_password(const char *username);
esp_err_t gateway_auth_set_password(const char *username, const char *new_password);
esp_err_t gateway_auth_change_password(const char *username, const char *current_password,
                                       const char *new_password);
esp_err_t gateway_auth_restore_defaults(void);
