#include "gateway_admin_http.h"

#include <string.h>

#include "gateway_auth.h"
#include "gateway_web_theme.h"

static const char ADMIN_PAGE[] =
"<!doctype html><html lang='vi'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta name='color-scheme' content='dark'><title>AUBOT | Quản lý tài khoản</title><link rel='stylesheet' href='/ui.css'><style>"
".users{display:grid;gap:10px}.user-row{display:grid;grid-template-columns:minmax(150px,1fr) 100px 100px auto;align-items:center;gap:10px;padding:13px;border:1px solid var(--line2);border-radius:11px;background:var(--glass2)}.user-row small{color:var(--muted)}.actions{display:flex;gap:7px;justify-content:flex-end;flex-wrap:wrap}@media(max-width:720px){.user-row{grid-template-columns:1fr 1fr}.actions{grid-column:1/-1;justify-content:stretch}.actions .btn{flex:1}}</style></head>"
"<body data-page='manage'><div class='shell'><header class='hero'><div class='brand'><img class='logo' src='/logo.png' alt='AUBOT'><div><div class='eyebrow'>AUBOT · WAREHOUSE GATEWAY</div><h1>Quản lý hệ thống</h1><p>Tài khoản và quyền truy cập Gateway</p></div></div><div data-app-user></div></header><nav class='nav' data-app-nav aria-label='Điều hướng chính'><a href='/'>TỔNG QUAN</a></nav>"
"<main class='grid-2'><section class='card'><header class='card-head'><span class='step'>01</span><h2>Tài khoản hệ thống</h2></header><p class='hint'>Tắt tài khoản phụ hoặc đặt lại mật khẩu mặc định. Tài khoản admin_aubot luôn được bảo vệ.</p><div id='users' class='users'></div><div id='feedback' class='message' role='status'></div></section>"
"<section class='card'><header class='card-head'><span class='step'>02</span><h2>Đặt mật khẩu mới</h2></header><form id='passwordForm' class='fields'><label class='field full'><span>Tài khoản</span><select id='username'></select></label><label class='field full'><span>Mật khẩu mới</span><input id='password' type='password' minlength='8' maxlength='64' autocomplete='new-password' required></label><label class='field full'><span>Nhập lại mật khẩu</span><input id='confirmPassword' type='password' minlength='8' maxlength='64' autocomplete='new-password' required></label><button class='btn primary full'>Lưu mật khẩu mới</button></form></section></main></div>"
"<script src='/ui.js'></script><script>const $=x=>document.getElementById(x);let users=[];function role(x){return {FACTORY:'Nhà máy',TECH:'Kỹ thuật',IT:'Hạ tầng',AUBOT:'Quản trị'}[x]||x}async function request(url,data){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});if(!r.ok)throw Error(await r.text());return r}function render(){users.innerHTML;$('users').innerHTML=users.map(u=>`<div class='user-row'><div><b>${u.username}</b><br><small>${role(u.role)}</small></div><span class='badge'>${u.enabled?'ĐANG BẬT':'ĐÃ TẮT'}</span><span class='dot ${u.enabled?'ok':'bad'}'></span><div class='actions'><button class='btn ghost' data-reset='${u.username}'>Đặt lại</button>${u.role==='AUBOT'?'':`<button class='btn ${u.enabled?'danger':'secondary'}' data-toggle='${u.username}' data-enabled='${u.enabled?0:1}'>${u.enabled?'Tắt':'Bật'}</button>`}</div></div>`).join('');$('username').innerHTML=users.map(u=>`<option>${u.username}</option>`).join('');document.querySelectorAll('[data-reset]').forEach(b=>b.onclick=()=>reset(b.dataset.reset));document.querySelectorAll('[data-toggle]').forEach(b=>b.onclick=()=>toggle(b.dataset.toggle,b.dataset.enabled))}async function load(){const r=await fetch('/api/admin/users',{cache:'no-store'});if(!r.ok)throw Error(await r.text());users=(await r.json()).users;render()}async function reset(u){if(!confirm('Đặt lại mật khẩu mặc định cho '+u+'? Tất cả phiên của tài khoản này sẽ đăng xuất.'))return;try{await request('/api/admin/user/password',{username:u,reset:1});$('feedback').textContent='Đã đặt lại mật khẩu cho '+u;$('feedback').className='message ok'}catch(e){$('feedback').textContent=e.message;$('feedback').className='message bad'}}async function toggle(u,v){if(!confirm((Number(v)?'Bật ':'Tắt ')+u+'?'))return;try{await request('/api/admin/user/enabled',{username:u,enabled:v});await load()}catch(e){$('feedback').textContent=e.message;$('feedback').className='message bad'}}$('passwordForm').onsubmit=async e=>{e.preventDefault();if($('password').value!==$('confirmPassword').value){$('feedback').textContent='Mật khẩu nhập lại không khớp.';$('feedback').className='message bad';return}try{await request('/api/admin/user/password',{username:$('username').value,new_password:$('password').value});$('passwordForm').reset();$('feedback').textContent='Đã đổi mật khẩu và đăng xuất các phiên cũ.';$('feedback').className='message ok'}catch(x){$('feedback').textContent=x.message;$('feedback').className='message bad'}};load().catch(e=>{$('feedback').textContent=e.message;$('feedback').className='message bad'})</script></body></html>";

static bool read_body(httpd_req_t *request, char *body, size_t capacity)
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

static bool field(const char *body, const char *key, char *output, size_t capacity)
{
    if (httpd_query_key_value(body, key, output, capacity) != ESP_OK) return false;
    char *input = output, *decoded = output;
    while (*input) {
        if (*input == '+') {
            *decoded++ = ' ';
            ++input;
        } else if (*input == '%' && input[1] && input[2]) {
            const char high = input[1], low = input[2];
            const int high_value = high >= '0' && high <= '9' ? high - '0' :
                high >= 'a' && high <= 'f' ? high - 'a' + 10 :
                high >= 'A' && high <= 'F' ? high - 'A' + 10 : -1;
            const int low_value = low >= '0' && low <= '9' ? low - '0' :
                low >= 'a' && low <= 'f' ? low - 'a' + 10 :
                low >= 'A' && low <= 'F' ? low - 'A' + 10 : -1;
            if (high_value < 0 || low_value < 0) return false;
            *decoded++ = (char)((high_value << 4) | low_value);
            input += 3;
        } else {
            *decoded++ = *input++;
        }
    }
    *decoded = '\0';
    return true;
}

static esp_err_t page(httpd_req_t *request)
{
    if (!gateway_auth_require_page(request, GW_PERMISSION_USER_MANAGEMENT, NULL)) return ESP_OK;
    return gateway_web_send_html(request, ADMIN_PAGE);
}

static esp_err_t users_get(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_USER_MANAGEMENT, NULL)) return ESP_OK;
    gateway_auth_account_info_t accounts[4];
    const size_t count = gateway_auth_accounts(accounts, 4U);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t error = httpd_resp_send_chunk(request, "{\"users\":[", 10U);
    char json[128];
    for (size_t i = 0; error == ESP_OK && i < count; ++i) {
        const int length = snprintf(json, sizeof(json),
            "%s{\"username\":\"%s\",\"role\":\"%s\",\"enabled\":%s}",
            i ? "," : "", accounts[i].username, gateway_auth_role_name(accounts[i].role),
            accounts[i].enabled ? "true" : "false");
        error = httpd_resp_send_chunk(request, json, length);
    }
    if (error == ESP_OK) error = httpd_resp_send_chunk(request, "]}", 2U);
    if (error == ESP_OK) error = httpd_resp_send_chunk(request, NULL, 0U);
    return error;
}

static esp_err_t enabled_post(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_USER_MANAGEMENT, NULL)) return ESP_OK;
    char body[96] = {0}, username[24] = {0}, enabled[4] = {0};
    if (!read_body(request, body, sizeof(body)) ||
        !field(body, "username", username, sizeof(username)) ||
        !field(body, "enabled", enabled, sizeof(enabled)))
        return gateway_web_send_text(request, "400 Bad Request", "Dữ liệu không hợp lệ");
    const esp_err_t error = gateway_auth_set_enabled(username, strcmp(enabled, "1") == 0);
    if (error != ESP_OK)
        return gateway_web_send_text(request, "400 Bad Request",
                                     "Không thể đổi trạng thái tài khoản");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t password_post(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_USER_MANAGEMENT, NULL)) return ESP_OK;
    char body[320] = {0}, username[24] = {0}, password[65] = {0}, reset[4] = {0};
    if (!read_body(request, body, sizeof(body)) ||
        !field(body, "username", username, sizeof(username)))
        return gateway_web_send_text(request, "400 Bad Request", "Dữ liệu không hợp lệ");
    const esp_err_t error = field(body, "reset", reset, sizeof(reset)) && strcmp(reset, "1") == 0
        ? gateway_auth_reset_password(username)
        : field(body, "new_password", password, sizeof(password))
            ? gateway_auth_set_password(username, password) : ESP_ERR_INVALID_ARG;
    if (error != ESP_OK)
        return gateway_web_send_text(request, "400 Bad Request",
                                     "Mật khẩu phải từ 8 đến 64 ký tự");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

esp_err_t gateway_admin_http_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri="/app/manage",.method=HTTP_GET,.handler=page},
        {.uri="/api/admin/users",.method=HTTP_GET,.handler=users_get},
        {.uri="/api/admin/user/password",.method=HTTP_POST,.handler=password_post},
        {.uri="/api/admin/user/enabled",.method=HTTP_POST,.handler=enabled_post},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const esp_err_t error = httpd_register_uri_handler(server, &handlers[i]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
