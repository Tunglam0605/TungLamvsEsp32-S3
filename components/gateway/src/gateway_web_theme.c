#include "gateway_web_theme.h"

#include <stdio.h>
#include <string.h>

#include "gateway_config.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"

static const char UI_CSS[] =
":root{--bg0:#0b1220;--bg1:#111a2d;--glass:rgba(22,33,53,.92);--glass2:#10192a;--line:#30415c;--line2:#25344b;--text:#f8fafc;--muted:#a9b7ca;--blue:#60a5fa;--violet:#8b7cf6;--cyan:#39cdf8;--danger:#f87171;--success:#34d399;--warning:#fbbf24;--radius:17px;--radius-sm:10px;font-family:system-ui,-apple-system,'Segoe UI',Arial,sans-serif;color-scheme:dark}"
"*{box-sizing:border-box}html{min-height:100%;background:var(--bg0)}body{min-height:100vh;margin:0;background:radial-gradient(circle at 12% 0,rgba(124,92,246,.16),transparent 34%),radial-gradient(circle at 90% 4%,rgba(57,205,248,.11),transparent 30%),linear-gradient(145deg,var(--bg0),#0e1728 52%,#0a1220);color:var(--text);font:14px/1.5 system-ui,-apple-system,'Segoe UI',Arial,sans-serif}button,input,select{font:inherit}button,a{touch-action:manipulation}:focus-visible{outline:3px solid rgba(57,205,248,.28);outline-offset:2px}.shell{width:min(1180px,calc(100% - 40px));margin-inline:auto;padding:20px 0 40px}.hero,.card,dialog,.toolbar{border:1px solid var(--line2);background:var(--glass);box-shadow:0 12px 32px rgba(2,6,23,.18);backdrop-filter:blur(10px)}.hero{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:18px;padding:17px 19px;border-radius:var(--radius)}.brand{display:flex;align-items:center;gap:15px;min-width:0}.logo{width:154px;height:40px;object-fit:contain;object-position:left center}.eyebrow{color:#a5b4fc;font-size:11px;font-weight:750;letter-spacing:.14em;text-transform:uppercase}.hero h1{margin:2px 0 0;font-size:25px;line-height:1.18}.hero p{margin:4px 0 0;color:var(--muted)}.header-status{display:flex;align-items:center;justify-content:flex-end;gap:8px;flex-wrap:wrap}.status-chip{display:grid;grid-template-columns:auto 1fr;gap:1px 8px;align-items:center;min-height:48px;padding:7px 12px;border:1px solid var(--line);border-radius:11px;background:var(--glass2);white-space:nowrap}.status-chip i,.dot{width:8px;height:8px;border-radius:50%;background:#64748b}.status-chip i{grid-row:1/3}.status-chip small{color:var(--muted);font-size:10px;text-transform:uppercase}.status-chip b{font-size:12px}.status-chip.ok i,.dot.ok{background:var(--success);box-shadow:0 0 0 3px rgba(52,211,153,.12)}.status-chip.warn i,.dot.warn{background:var(--warning)}.status-chip.bad i,.dot.bad{background:var(--danger)}.status-chip.identity i{background:var(--cyan)}.nav{display:flex;gap:8px;margin:12px 0 16px;padding:5px;border:1px solid var(--line2);border-radius:12px;background:rgba(12,20,34,.72)}.nav a{display:grid;place-items:center;min-height:42px;padding:0 16px;border-radius:8px;color:var(--muted);font-size:12px;font-weight:800;letter-spacing:.04em;text-decoration:none}.nav a:hover{background:rgba(96,165,250,.08);color:var(--text)}.nav a.active{background:linear-gradient(135deg,rgba(96,165,250,.18),rgba(139,124,246,.18));color:#e0e7ff}.layout,.grid-2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{padding:18px;border-radius:var(--radius)}.card-head{display:flex;align-items:center;gap:10px;margin-bottom:14px}.card-head h2,.card h2{margin:0;font-size:17px}.step{display:grid;place-items:center;flex:0 0 29px;width:29px;height:29px;border:1px solid rgba(96,165,250,.52);border-radius:8px;background:rgba(96,165,250,.09);color:#bfdbfe;font-size:11px;font-weight:800}.muted,.hint{color:var(--muted)}.hint{margin:6px 0 0;font-size:12px}.fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:11px}.field{display:grid;align-content:start;gap:6px}.field.full,.full{grid-column:1/-1}.field label,.field-label{color:#d6deeb;font-size:12px;font-weight:700}.field input,.field select,.input{width:100%;min-height:46px;border:1px solid var(--line);border-radius:9px;background:var(--glass2);color:var(--text);padding:9px 11px}.field input::placeholder{color:#748399}.field input:hover,.field select:hover{border-color:#536784}.field input:focus-visible,.field select:focus-visible{border-color:var(--cyan)}.btn{display:inline-flex;align-items:center;justify-content:center;min-height:44px;border:1px solid var(--line);border-radius:9px;padding:8px 14px;background:transparent;color:var(--text);font-weight:750;cursor:pointer;transition:background .18s,border-color .18s,color .18s}.btn:hover:not(:disabled){border-color:var(--blue);background:rgba(96,165,250,.08)}.btn:disabled{cursor:not-allowed;opacity:.52}.primary{border-color:#2563eb;background:#2563eb;color:#fff}.primary:hover:not(:disabled){background:#1d4ed8;color:#fff}.secondary{background:rgba(96,165,250,.08);color:#bfdbfe}.ghost{background:transparent}.danger{color:#fecaca;border-color:rgba(248,113,113,.4)}.message{min-height:22px;margin:10px 0 0;color:var(--muted);font-size:13px}.message.ok{color:#86efac}.message.bad{color:#fca5a5}.toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px;padding:12px 14px;border-radius:13px}.segmented{display:flex;gap:5px;padding:4px;border:1px solid var(--line2);border-radius:10px;background:var(--glass2)}.segmented .btn{min-height:38px;border:0;padding:6px 13px;color:var(--muted)}.segmented .btn.active{background:rgba(96,165,250,.18);color:#e0e7ff}.summary-line{color:var(--muted);font-size:13px}.summary-line b{color:var(--text)}.badge{display:inline-flex;align-items:center;min-height:27px;padding:3px 8px;border:1px solid var(--line);border-radius:999px;color:var(--muted);font-size:11px}.profiles{display:flex;flex-wrap:wrap;gap:7px;margin-top:9px}.profile-chip{min-height:30px;padding:5px 9px;border:1px solid var(--line2);border-radius:999px;background:var(--glass2);color:#cbd5e1;font-size:11px}.hidden{display:none!important}details{border:1px solid var(--line2);border-radius:11px;background:rgba(10,17,30,.35)}details>summary{min-height:46px;display:flex;align-items:center;padding:0 13px;color:#cbd5e1;font-weight:750;cursor:pointer}details[open]>summary{border-bottom:1px solid var(--line2)}.details-body{padding:13px}.online-text{color:var(--success)}.warning-text{color:var(--warning)}.danger-text{color:var(--danger)}dialog{width:min(650px,calc(100% - 24px));max-height:calc(100vh - 30px);overflow:auto;padding:0;border-radius:18px;color:var(--text)}dialog::backdrop{background:rgba(2,6,23,.76)}.dialog-head{position:sticky;top:0;z-index:2;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:16px 18px;border-bottom:1px solid var(--line2);background:#162136}.dialog-head h2{margin:0;font-size:19px}.dialog-body{padding:18px}.dialog-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}.switch-line{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:46px;padding:8px 11px;border:1px solid var(--line2);border-radius:9px;background:var(--glass2)}.switch-line input{width:20px;height:20px;accent-color:var(--blue)}.user-box{display:flex;align-items:center;gap:9px;padding:7px 10px;border:1px solid var(--line);border-radius:10px;background:var(--glass2)}.user-box small{display:block;color:var(--muted);font-size:10px}.user-box b{display:block;font-size:12px}.nav form{margin-left:auto}.nav button{min-height:42px}@media(max-width:900px){.hero{grid-template-columns:1fr}.header-status{justify-content:flex-start}.layout,.grid-2{grid-template-columns:1fr}}@media(max-width:680px){.shell{width:calc(100% - 24px);padding-top:12px}.hero{padding:15px}.brand{align-items:flex-start}.logo{width:116px;height:34px}.hero h1{font-size:21px}.header-status{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));width:100%}.status-chip{min-width:0;padding:7px 9px}.status-chip b{overflow:hidden;text-overflow:ellipsis}.nav{display:grid;grid-template-columns:repeat(3,1fr)}.nav a{padding:0 6px}.toolbar{align-items:flex-start;flex-direction:column}.fields{grid-template-columns:1fr}.field.full,.full{grid-column:auto}.dialog-actions{display:grid;grid-template-columns:1fr 1fr}.dialog-actions .btn{width:100%}}@media(max-width:420px){.shell{width:calc(100% - 20px)}.header-status{grid-template-columns:1fr}.brand{gap:10px}.logo{width:100px}.nav a{font-size:10px}.card{padding:15px}.dialog-body{padding:15px}}@media(prefers-reduced-motion:reduce){*{transition:none!important;scroll-behavior:auto!important}}";

extern const unsigned char company_logo_start[]
    asm("_binary_company_logo_transparent_png_start");
extern const unsigned char company_logo_end[]
    asm("_binary_company_logo_transparent_png_end");

static const char UI_JS[] =
"(()=>{const page=document.body.dataset.page||'';fetch('/api/auth/me',{cache:'no-store'}).then(r=>r.json()).then(me=>{"
"const nav=document.querySelector('[data-app-nav]'),user=document.querySelector('[data-app-user]'),action=document.querySelector('[data-public-auth]');"
"if(user&&me.authenticated)user.innerHTML='<div class=\"user-box\"><span class=\"dot ok\"></span><span><small>'+me.role+'</small><b>'+me.username+'</b></span></div>';"
"if(action)action.innerHTML=me.authenticated?'<a class=\"btn secondary\" href=\"'+me.home+'\">Ứng dụng</a><form method=\"post\" action=\"/logout\"><button class=\"btn ghost\">Đăng xuất</button></form>':'<a class=\"btn primary\" href=\"/login\">Đăng nhập</a>';"
"if(!nav||!me.authenticated)return;let a=[['overview','/','TỔNG QUAN']];if(me.role==='FACTORY')a.push(['factory','/app/factory','THIẾT LẬP KHO']);if(me.role==='TECH')a.push(['tech','/app/tech','CHẨN ĐOÁN']);if(me.role==='IT')a.push(['it','/app/it','KẾT NỐI']);if(me.role==='AUBOT')a.push(['factory','/app/factory','THIẾT LẬP KHO'],['tech','/app/tech','CHẨN ĐOÁN'],['it','/app/it','KẾT NỐI'],['manage','/app/manage','QUẢN LÝ']);nav.innerHTML=a.map(x=>'<a class=\"'+(x[0]===page?'active':'')+'\" href=\"'+x[1]+'\">'+x[2]+'</a>').join('')+'<button class=\"btn ghost\" type=\"button\" data-change-password>Đổi mật khẩu</button><form method=\"post\" action=\"/logout\"><button class=\"btn ghost\">Đăng xuất</button></form>';const dialog=document.createElement('dialog');dialog.innerHTML='<header class=\"dialog-head\"><h2>Đổi mật khẩu</h2><button class=\"btn ghost\" type=\"button\" data-pw-close>Đóng</button></header><form class=\"dialog-body fields\" data-pw-form><label class=\"field full\"><span>Mật khẩu hiện tại</span><input name=\"current_password\" type=\"password\" maxlength=\"64\" autocomplete=\"current-password\" required></label><label class=\"field full\"><span>Mật khẩu mới</span><input name=\"new_password\" type=\"password\" minlength=\"8\" maxlength=\"64\" autocomplete=\"new-password\" required></label><label class=\"field full\"><span>Nhập lại mật khẩu</span><input name=\"confirm_password\" type=\"password\" minlength=\"8\" maxlength=\"64\" autocomplete=\"new-password\" required></label><div class=\"message full\" data-pw-message></div><button class=\"btn primary full\">Lưu mật khẩu mới</button></form>';document.body.append(dialog);const form=dialog.querySelector('[data-pw-form]'),message=dialog.querySelector('[data-pw-message]');nav.querySelector('[data-change-password]').onclick=()=>{form.reset();message.textContent='';dialog.showModal()};dialog.querySelector('[data-pw-close]').onclick=()=>dialog.close();form.onsubmit=async event=>{event.preventDefault();const data=new FormData(form);if(data.get('new_password')!==data.get('confirm_password')){message.textContent='Mật khẩu nhập lại không khớp.';message.className='message bad full';return}const body=new URLSearchParams({current_password:data.get('current_password'),new_password:data.get('new_password')});const r=await fetch('/api/auth/password',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok){message.textContent=await r.text();message.className='message bad full';return}location='/login'};"
"}).catch(()=>{})})();";

static esp_err_t css_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/css; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=3600");
    return httpd_resp_send(request, UI_CSS, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t logo_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "image/png");
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(request, (const char *)company_logo_start,
                           company_logo_end - company_logo_start);
}

static esp_err_t js_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=3600");
    return httpd_resp_send(request, UI_JS, HTTPD_RESP_USE_STRLEN);
}

esp_err_t gateway_web_send_html(httpd_req_t *request, const char *html)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const size_t total = strlen(html);
    for (size_t offset = 0; offset < total; offset += 2048U) {
        const size_t remaining = total - offset;
        const size_t length = remaining > 2048U ? 2048U : remaining;
        const esp_err_t error = httpd_resp_send_chunk(request, html + offset, length);
        if (error != ESP_OK) return error;
    }
    return httpd_resp_send_chunk(request, NULL, 0U);
}

static esp_err_t ui_status_handler(httpd_req_t *request)
{
    gateway_config_t config;
    gateway_config_get(&config);
    char gateway_id[40], network_name[96];
    size_t gw_used = 0U, network_used = 0U;
    for (const char *p = config.gateway_id; *p && gw_used + 2U < sizeof(gateway_id); ++p) {
        if (*p == '"' || *p == '\\') gateway_id[gw_used++] = '\\';
        gateway_id[gw_used++] = *p;
    }
    gateway_id[gw_used] = '\0';
    for (const char *p = gateway_network_active_name(); *p && network_used + 2U < sizeof(network_name); ++p) {
        if (*p == '"' || *p == '\\') network_name[network_used++] = '\\';
        network_name[network_used++] = *p;
    }
    network_name[network_used] = '\0';
    char json[256];
    const int length = snprintf(json, sizeof(json),
        "{\"gateway_id\":\"%s\",\"network\":%s,\"network_name\":\"%s\",\"mqtt\":%s}",
        gateway_id, gateway_network_production_available() ? "true" : "false",
        network_name, gateway_mqtt_is_connected() ? "true" : "false");
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, length);
}

esp_err_t gateway_web_theme_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/ui.css", .method = HTTP_GET, .handler = css_handler},
        {.uri = "/logo.png", .method = HTTP_GET, .handler = logo_handler},
        {.uri = "/ui.js", .method = HTTP_GET, .handler = js_handler},
        {.uri = "/api/ui/status", .method = HTTP_GET, .handler = ui_status_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const esp_err_t error = httpd_register_uri_handler(server, &handlers[i]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
