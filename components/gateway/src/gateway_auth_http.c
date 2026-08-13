#include "gateway_auth_http.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "esp_timer.h"
#include "gateway_auth.h"
#include "gateway_web_theme.h"

static const char LOGIN_PAGE_HEAD[] =
"<!doctype html><html lang='vi'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta name='color-scheme' content='dark'><title>AUBOT | Đăng nhập</title><link rel='stylesheet' href='/ui.css?v=5'><style>body{display:grid;place-items:center;padding:18px}.login{width:min(410px,100%);padding:26px}.login .logo{display:block;width:172px;height:44px;margin-bottom:20px}.login h1{margin:5px 0 4px;font-size:24px}.login form{display:grid;gap:13px;margin-top:20px}.login .btn{width:100%;margin-top:4px}</style></head><body><main class='card login'><img class='logo' src='/logo.png' alt='AUBOT'><div class='eyebrow'>AUBOT · WAREHOUSE GATEWAY</div><h1>Đăng nhập</h1><p class='muted'>Quyền truy cập được xác định theo tài khoản.</p><form method='post' action='/login'><label class='field'><span>Tài khoản</span><input name='username' maxlength='23' autocomplete='username' required autofocus></label><label class='field'><span>Mật khẩu</span><input name='password' type='password' maxlength='64' autocomplete='current-password' required></label><button class='btn primary'>Đăng nhập</button></form></main></body></html>";

static esp_err_t send_login_page(httpd_req_t *request, const char *error)
{
    if (error == NULL || error[0] == '\0') return gateway_web_send_html(request, LOGIN_PAGE_HEAD);
    const char *anchor = "<form method='post'";
    const char *at = strstr(LOGIN_PAGE_HEAD, anchor);
    if (at == NULL) return gateway_web_send_html(request, LOGIN_PAGE_HEAD);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send_chunk(request, LOGIN_PAGE_HEAD,
                                              (ssize_t)(at - LOGIN_PAGE_HEAD));
    if (result == ESP_OK) result = httpd_resp_send_chunk(request,
        "<p class='message bad' role='alert'>", HTTPD_RESP_USE_STRLEN);
    if (result == ESP_OK) result = httpd_resp_send_chunk(request, error, HTTPD_RESP_USE_STRLEN);
    if (result == ESP_OK) result = httpd_resp_send_chunk(request, "</p>", 4);
    if (result == ESP_OK) result = httpd_resp_send_chunk(request, at, HTTPD_RESP_USE_STRLEN);
    if (result == ESP_OK) result = httpd_resp_send_chunk(request, NULL, 0);
    return result;
}

static uint32_t source_ipv4(httpd_req_t *request)
{
    const int socket = httpd_req_to_sockfd(request);
    struct sockaddr_in peer = {0};
    socklen_t length = sizeof(peer);
    return socket >= 0 && getpeername(socket, (struct sockaddr *)&peer, &length) == 0 &&
           peer.sin_family == AF_INET ? peer.sin_addr.s_addr : 0U;
}

static bool body_read(httpd_req_t *request, char *body, size_t capacity)
{
    if (!request->content_len || request->content_len >= capacity) return false;
    size_t total = 0U;
    while (total < request->content_len) {
        const int received = httpd_req_recv(request, body + total, request->content_len - total);
        if (received <= 0) return false;
        total += (size_t)received;
    }
    body[total] = '\0';
    return true;
}

static int hex(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void decode(char *text)
{
    char *output = text;
    for (; *text; ++text) {
        if (*text == '+') *output++ = ' ';
        else if (*text == '%' && hex(text[1]) >= 0 && hex(text[2]) >= 0) {
            *output++ = (char)((hex(text[1]) << 4) | hex(text[2]));
            text += 2;
        } else *output++ = *text;
    }
    *output = '\0';
}

static bool field(const char *body, const char *key, char *output, size_t capacity)
{
    if (httpd_query_key_value(body, key, output, capacity) != ESP_OK) return false;
    decode(output);
    return true;
}

static const char *home(gateway_role_t role)
{
    switch (role) {
    case GW_ROLE_FACTORY: return "/app/factory";
    case GW_ROLE_TECH: return "/app/tech";
    case GW_ROLE_IT: return "/app/it";
    case GW_ROLE_SUPER_ADMIN: return "/";
    default: return "/";
    }
}

static esp_err_t login_get(httpd_req_t *request)
{
    gateway_auth_session_t session;
    if (gateway_auth_session_from_request(request, &session)) {
        httpd_resp_set_status(request, "303 See Other");
        httpd_resp_set_hdr(request, "Location", home(session.role));
        return httpd_resp_sendstr(request, "");
    }
    if (!gateway_auth_is_ready())
        return gateway_web_send_text(request, "503 Service Unavailable",
                                     "Dịch vụ đăng nhập chưa sẵn sàng");
    return send_login_page(request, NULL);
}

static esp_err_t login_post(httpd_req_t *request)
{
    const int64_t now = esp_timer_get_time() / 1000LL;
    const uint32_t source = source_ipv4(request);
    uint32_t retry = 0U;
    if (!gateway_auth_is_ready())
        return gateway_web_send_text(request, "503 Service Unavailable",
                                     "Dịch vụ đăng nhập chưa sẵn sàng");
    if (!gateway_auth_login_allowed(source, now, &retry)) {
        char value[16];
        snprintf(value, sizeof(value), "%" PRIu32, retry);
        httpd_resp_set_hdr(request, "Retry-After", value);
        char message[96];
        snprintf(message, sizeof(message), "Đăng nhập tạm khóa. Thử lại sau %u giây.",
                 (unsigned)retry);
        httpd_resp_set_status(request, "429 Too Many Requests");
        return send_login_page(request, message);
    }
    char body[320] = {0}, username[24] = {0}, password[65] = {0}, token[65];
    gateway_auth_session_t session;
    if (!body_read(request, body, sizeof(body)) ||
        !field(body, "username", username, sizeof(username)) ||
        !field(body, "password", password, sizeof(password)) ||
        !gateway_auth_authenticate(username, password, &session, token, sizeof(token))) {
        gateway_auth_login_failed(source, now);
        httpd_resp_set_status(request, "401 Unauthorized");
        return send_login_page(request, "Sai tài khoản hoặc mật khẩu.");
    }
    gateway_auth_login_succeeded(source);
    char cookie[160];
    snprintf(cookie, sizeof(cookie), "GWSESSION=%s; Max-Age=1800; HttpOnly; SameSite=Strict; Path=/", token);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", home(session.role));
    return httpd_resp_sendstr(request, "");
}

static esp_err_t logout_post(httpd_req_t *request)
{
    gateway_auth_logout_request(request);
    httpd_resp_set_hdr(request, "Set-Cookie", "GWSESSION=; Max-Age=0; HttpOnly; SameSite=Strict; Path=/");
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_sendstr(request, "");
}

static esp_err_t me_get(httpd_req_t *request)
{
    gateway_auth_session_t session;
    char json[180];
    if (!gateway_auth_session_from_request(request, &session)) {
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, "{\"authenticated\":false,\"role\":\"PUBLIC\",\"home\":\"/login\"}");
    }
    const int length = snprintf(json, sizeof(json),
        "{\"authenticated\":true,\"username\":\"%s\",\"role\":\"%s\",\"home\":\"%s\"}",
        session.username, gateway_auth_role_name(session.role), home(session.role));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, length);
}

esp_err_t gateway_auth_http_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri="/login",.method=HTTP_GET,.handler=login_get},
        {.uri="/login",.method=HTTP_POST,.handler=login_post},
        {.uri="/logout",.method=HTTP_POST,.handler=logout_post},
        {.uri="/api/auth/me",.method=HTTP_GET,.handler=me_get},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const esp_err_t error = httpd_register_uri_handler(server, &handlers[i]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
