#include "gateway_web_theme.h"

#include <stdio.h>
#include <string.h>

#include "gateway_config.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "bsp_can.h"
#include "bsp_eth.h"

static const char UI_CSS[] =
/* Shared AUBOT company palette and motion language, aligned with Callbox. */
":root{--bg0:#0d1525;--bg1:#153352;--glass:#172236;--glass2:#111a2c;--line:#3b4b64;--line2:#2d3b52;--text:#f8fafc;--muted:#a9b7ca;--blue:#34d399;--violet:#047857;--cyan:#39cdf8;--danger:#f87171;--success:#34d399;--warning:#fbbf24;--radius:12px;--radius-sm:9px;--shadow:0 16px 40px rgba(2,6,23,.22);font-family:system-ui,-apple-system,'Segoe UI',Arial,sans-serif;color-scheme:dark}"
"*{box-sizing:border-box}html{min-height:100%;background:var(--bg0)}body{min-height:100vh;margin:0;background:radial-gradient(circle at 78% 8%,#153352 0,transparent 35%),var(--bg0);color:var(--text);font:14px/1.5 system-ui,-apple-system,'Segoe UI',Arial,sans-serif}button,input,select{font:inherit}button,a{touch-action:manipulation}:focus-visible{outline:3px solid rgba(57,205,248,.22);outline-offset:2px}.shell{width:min(1440px,calc(100% - 48px));margin-inline:auto;padding:22px 0 38px}.hero,.card,dialog,.toolbar{border:1px solid var(--line2);background:var(--glass);box-shadow:var(--shadow)}.hero{display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:center;gap:20px;padding:18px 20px;border-radius:var(--radius)}.brand{display:flex;align-items:center;gap:15px;min-width:0}.logo{width:154px;height:40px;object-fit:contain;object-position:left center}.eyebrow{color:#92a8e8;font-size:11px;font-weight:750;letter-spacing:.15em;text-transform:uppercase}.hero h1{margin:2px 0 0;font-size:26px;line-height:1.15}.hero p{margin:4px 0 0;color:var(--muted)}.header-status{display:flex;align-items:center;justify-content:flex-end;gap:8px;flex-wrap:wrap}.status-chip{display:grid;grid-template-columns:auto 1fr;gap:1px 8px;align-items:center;min-height:50px;padding:8px 13px;border:1px solid var(--line);border-radius:11px;background:var(--glass2);white-space:nowrap}.status-chip i,.dot{width:9px;height:9px;border-radius:50%;background:#64748b}.status-chip i{grid-row:1/3}.status-chip small{color:var(--muted);font-size:10px;text-transform:uppercase}.status-chip b{font-size:13px}.status-chip.ok i,.dot.ok{background:var(--success);box-shadow:0 0 0 4px rgba(52,211,153,.12)}.status-chip.warn i,.dot.warn{background:var(--warning)}.status-chip.bad i,.dot.bad{background:var(--danger)}.status-chip.identity i{background:var(--cyan);box-shadow:0 0 0 4px rgba(57,205,248,.12)}.nav{display:flex;gap:8px;margin:12px 0 16px;padding:5px;border:1px solid var(--line2);border-radius:10px;background:var(--glass2)}.nav a{display:grid;place-items:center;min-height:42px;padding:0 16px;border-radius:8px;color:var(--muted);font-size:12px;font-weight:800;letter-spacing:.04em;text-decoration:none;transition:background .18s,color .18s}.nav a:hover{background:rgba(52,211,153,.08);color:var(--success)}.nav a.active{background:rgba(52,211,153,.12);color:#a7f3d0}.layout,.grid-2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{padding:18px;border-radius:var(--radius)}.card-head{display:flex;align-items:center;gap:10px;margin-bottom:14px}.card-head h2,.card h2{margin:0;font-size:17px}.step{display:grid;place-items:center;flex:0 0 29px;width:29px;height:29px;border:1px solid rgba(52,211,153,.55);border-radius:8px;background:rgba(52,211,153,.1);color:var(--success);font-size:11px;font-weight:800}.muted,.hint{color:var(--muted)}.hint{margin:6px 0 0;font-size:12px}.fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:11px}.field{display:grid;align-content:start;gap:6px}.field.full,.full{grid-column:1/-1}.field label,.field-label{color:var(--muted);font-size:13px;font-weight:650}.field input,.field select,.input{width:100%;min-height:46px;border:1px solid var(--line);border-radius:9px;background:var(--glass2);color:var(--text);padding:9px 11px}.field input::placeholder{color:#8190a6}.field input:hover,.field select:hover{border-color:#64748b}.field input:focus-visible,.field select:focus-visible{border-color:var(--cyan);box-shadow:0 0 0 1px var(--cyan)}.btn{display:inline-flex;align-items:center;justify-content:center;min-height:46px;border:1px solid var(--line);border-radius:9px;padding:8px 14px;background:transparent;color:var(--text);font-weight:700;cursor:pointer;transition:background .18s,border-color .18s,color .18s,transform .18s}.btn:hover:not(:disabled){border-color:var(--success);background:rgba(52,211,153,.08);color:var(--success)}.btn:active:not(:disabled){transform:translateY(1px)}.btn:disabled{cursor:not-allowed;opacity:.6}.primary{border-color:#047857;background:#047857;color:#ecfdf5}.primary:hover:not(:disabled){border-color:var(--success);background:#065f46;color:#fff}.secondary,.ghost{background:transparent;color:var(--text)}.danger{color:#fca5a5;border-color:rgba(248,113,113,.4)}.danger:hover:not(:disabled){border-color:var(--danger);background:rgba(248,113,113,.08);color:#fecaca}.message{min-height:22px;margin:10px 0 0;color:var(--muted);font-size:13px}.message.ok{color:var(--success)}.message.bad{color:var(--danger)}.toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px;padding:12px 14px;border-radius:var(--radius)}.segmented{display:flex;gap:5px;padding:4px;border:1px solid var(--line2);border-radius:10px;background:var(--glass2)}.segmented .btn{min-height:38px;border:0;padding:6px 13px;color:var(--muted)}.segmented .btn.active{background:rgba(52,211,153,.14);color:#a7f3d0}.summary-line{color:var(--muted);font-size:13px}.summary-line b{color:var(--text)}.badge{display:inline-flex;align-items:center;min-height:27px;padding:3px 8px;border:1px solid var(--line);border-radius:999px;color:var(--muted);font-size:11px}.profiles{display:grid;gap:7px;margin-top:9px}.profile-row{display:grid;grid-template-columns:minmax(0,1fr) auto auto auto;align-items:center;gap:8px;min-height:48px;padding:7px 9px;border:1px solid var(--line2);border-radius:9px;background:var(--glass2)}.profile-row>span{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:650}.profile-row small{margin-left:7px;color:var(--success);font-weight:650}.profile-row .btn{min-height:34px;padding:5px 10px;font-size:12px}.hidden{display:none!important}details{border:1px solid var(--line2);border-radius:11px;background:rgba(17,26,44,.55)}details>summary{min-height:46px;display:flex;align-items:center;padding:0 13px;color:var(--text);font-weight:750;cursor:pointer}details[open]>summary{border-bottom:1px solid var(--line2)}.details-body{padding:13px}.online-text{color:var(--success)}.warning-text{color:var(--warning)}.danger-text{color:var(--danger)}dialog{width:min(650px,calc(100% - 24px));max-height:calc(100vh - 30px);overflow:auto;padding:0;border-radius:var(--radius);color:var(--text)}dialog::backdrop{background:rgba(2,6,23,.76)}.dialog-head{position:sticky;top:0;z-index:2;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:16px 18px;border-bottom:1px solid var(--line2);background:var(--glass)}.dialog-head h2{margin:0;font-size:19px}.dialog-body{padding:18px}.dialog-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}.switch-line{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:46px;padding:8px 11px;border:1px solid var(--line2);border-radius:9px;background:var(--glass2)}.switch-line input{width:20px;height:20px;accent-color:var(--success)}.user-box{display:flex;align-items:center;gap:9px;padding:7px 10px;border:1px solid var(--line);border-radius:10px;background:var(--glass2)}.user-box small{display:block;color:var(--muted);font-size:10px}.user-box b{display:block;font-size:12px}.user-menu{position:relative;margin-left:auto;border:0;background:transparent}.user-menu>summary{display:grid;min-width:130px;min-height:42px;padding:5px 28px 5px 10px;border:1px solid var(--line);border-radius:9px;background:var(--glass2);cursor:pointer;list-style:none}.user-menu>summary:after{content:'⌄';position:absolute;right:10px;top:10px;color:var(--muted)}.user-menu>summary span{font-weight:750}.user-menu>summary small{color:var(--muted);font-size:10px}.user-menu>div{position:absolute;z-index:20;right:0;top:calc(100% + 6px);display:grid;width:180px;padding:6px;border:1px solid var(--line);border-radius:10px;background:var(--glass);box-shadow:var(--shadow)}.user-menu .btn,.user-menu form{width:100%;margin:0}.user-menu .btn{justify-content:flex-start}.nav button{min-height:42px}.system-bar{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:6px;margin:10px 0}.system-item{display:flex;align-items:center;gap:7px;min-height:38px;padding:5px 9px;border:1px solid var(--line2);border-radius:9px;background:var(--glass2)}.system-item i{width:7px;height:7px;border-radius:50%;background:#64748b}.system-item.ok i{background:var(--success);box-shadow:0 0 0 3px rgba(52,211,153,.1)}.system-item.warn i{background:var(--warning);box-shadow:0 0 0 3px rgba(251,191,36,.1)}.system-item.bad i{background:var(--danger)}.system-item.stale{opacity:.72}.system-item small{display:block;color:var(--muted);font-size:9px}.system-item b{display:block;font-size:10px}@media(max-width:900px){.hero{grid-template-columns:1fr}.header-status{justify-content:flex-start}.layout,.grid-2{grid-template-columns:1fr}}@media(max-width:680px){.shell{width:calc(100% - 24px);padding-top:14px}.hero{padding:16px}.brand{align-items:flex-start}.logo{width:116px;height:34px}.hero h1{font-size:22px}.header-status{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));width:100%}.status-chip{min-width:0;padding:7px 9px}.status-chip b{overflow:hidden;text-overflow:ellipsis}.nav{display:flex;overflow:visible;flex-wrap:wrap}.nav a{flex:1;padding:0 6px}.user-menu{flex:1 0 100%}.user-menu>summary{width:100%}.user-menu>div{left:0;right:auto;width:100%}.system-bar{grid-template-columns:repeat(2,1fr)}.toolbar{align-items:flex-start;flex-direction:column}.fields{grid-template-columns:1fr}.field.full,.full{grid-column:auto}.profile-row{grid-template-columns:repeat(3,minmax(0,1fr))}.profile-row>span{grid-column:1/-1}.profile-row .btn{width:100%}.dialog-actions{display:grid;grid-template-columns:1fr 1fr}.dialog-actions .btn{width:100%}}@media(max-width:420px){.shell{width:calc(100% - 20px)}.header-status{grid-template-columns:1fr}.brand{gap:10px}.logo{width:100px}.nav a{font-size:10px}.card{padding:15px}.dialog-body{padding:15px}}@media(prefers-reduced-motion:reduce){*{transition:none!important;scroll-behavior:auto!important}}";

extern const unsigned char company_logo_start[]
    asm("_binary_company_logo_transparent_png_start");
extern const unsigned char company_logo_end[]
    asm("_binary_company_logo_transparent_png_end");

static const char UI_JS[] =
"const gw=(()=>{const fetchJson=async u=>{const r=await fetch(u,{cache:'no-store'});if(!r.ok)throw Error(await r.text());return r.json()};const page=document.body.dataset.page||'';const routes=[['overview','/','TỔNG QUAN'],['factory','/app/factory','KHO'],['tech','/app/tech','CHẨN ĐOÁN'],['it','/app/it','KẾT NỐI']];let me={authenticated:false,role:'PUBLIC'};function allowed(key){if(key==='overview')return true;if(!me.authenticated)return false;if(me.role==='AUBOT')return true;if(key==='factory')return me.role==='FACTORY'||me.role==='TECH';if(key==='tech')return me.role==='TECH';if(key==='it')return me.role==='FACTORY'||me.role==='TECH'||me.role==='IT';return false}function nav(){const n=document.querySelector('[data-app-nav]');if(!n)return;n.innerHTML=routes.map(x=>'<a class=\"'+(x[0]===page?'active':'')+'\" href=\"'+x[1]+'\" data-key=\"'+x[0]+'\">'+x[2]+'</a>').join('')+(me.authenticated?'<details class=\"user-menu\"><summary><span>'+me.username+'</span><small>'+me.role+'</small></summary><div><form method=\"post\" action=\"/logout\"><button class=\"btn ghost\">Đăng xuất</button></form></div></details>':'');n.querySelectorAll('[data-key]').forEach(a=>{const ok=allowed(a.dataset.key);if(me.authenticated&&!ok){a.setAttribute('aria-disabled','true');a.title='Tài khoản này không có quyền truy cập'}a.onclick=e=>{if(ok)return;e.preventDefault();if(!me.authenticated)location='/login?next='+encodeURIComponent(a.getAttribute('href'));else alert('Tài khoản này không có quyền vào trang đã chọn.')}})}function renderSystem(s){document.querySelectorAll('[data-system-bar]').forEach(bar=>{bar.className='system-bar';const values=[['Gateway',s.gateway_id,true],['Mạng',s.network?'ONLINE':'OFFLINE',s.network],['MQTT',s.mqtt?'ONLINE':'OFFLINE',s.mqtt],['CAN',s.can==='ACTIVE'?'HOẠT ĐỘNG':'LỖI',s.can==='ACTIVE'],['Ethernet',s.ethernet?'ONLINE':'OFFLINE',s.ethernet]];bar.innerHTML=values.map(x=>'<div class=\"system-item '+(x[2]?'ok':'bad')+'\"><i></i><span><small>'+x[0]+'</small><b>'+x[1]+'</b></span></div>').join('')})}async function system(){if(document.hidden){setTimeout(system,10000);return}try{const s=await fetchJson('/api/ui/status');sessionStorage.setItem('gwStatus',JSON.stringify(s));renderSystem(s)}catch(e){}setTimeout(system,4000)}try{const s=JSON.parse(sessionStorage.getItem('gwStatus'));if(s)renderSystem(s)}catch(e){}nav();fetchJson('/api/auth/me').then(x=>{me=x;const u=document.querySelector('[data-app-user]');if(u&&me.authenticated)u.innerHTML='<div class=\"user-box\"><span class=\"dot ok\"></span><span><small>'+me.role+'</small><b>'+me.username+'</b></span></div>';nav()}).catch(()=>{});system();return{fetchJson}})();";

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

esp_err_t gateway_web_send_text(httpd_req_t *request, const char *status,
                                const char *text)
{
    if (status != NULL) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, text != NULL ? text : "");
}

static esp_err_t ui_status_handler(httpd_req_t *request)
{
    gateway_config_t config;
    gateway_config_get(&config);
    char gateway_id[40];
    size_t gw_used = 0U;
    for (const char *p = config.gateway_id; *p && gw_used + 2U < sizeof(gateway_id); ++p) {
        if (*p == '"' || *p == '\\') gateway_id[gw_used++] = '\\';
        gateway_id[gw_used++] = *p;
    }
    gateway_id[gw_used] = '\0';
    bsp_can_status_t can = {0};
    bsp_eth_status_t ethernet = {0};
    bsp_can_get_status(&can);
    bsp_eth_get_status(&ethernet);
    char json[256];
    const int length = snprintf(json, sizeof(json),
        "{\"gateway_id\":\"%s\",\"network\":%s,\"mqtt\":%s,\"can\":\"%s\",\"ethernet\":%s}",
        gateway_id, gateway_network_production_available() ? "true" : "false",
        gateway_mqtt_is_connected() ? "true" : "false",
        can.state == BSP_CAN_STATE_ACTIVE ? "ACTIVE" :
        can.state == BSP_CAN_STATE_WARNING ? "WARNING" :
        can.state == BSP_CAN_STATE_PASSIVE ? "PASSIVE" :
        can.state == BSP_CAN_STATE_BUS_OFF ? "BUS-OFF" : "STOPPED",
        ethernet.connected ? "true" : "false");
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
