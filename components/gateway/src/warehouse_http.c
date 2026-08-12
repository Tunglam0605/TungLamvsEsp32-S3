#include "warehouse_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "warehouse_manager.h"
#include "gateway_warehouse_page.h"
#include "gateway_public_page.h"
#include "gateway_web_theme.h"
#include "gateway_auth.h"
#include "gateway_config.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "bsp_can.h"

#if 0
static const char PAGE[] =
"<!doctype html><html lang='vi'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Quản lý kho</title><style>"
":root{font-family:Segoe UI,system-ui;color:#eef4ff;background:#07111f}"
"*{box-sizing:border-box}body{margin:0;background:#07111f}.wrap{max-width:1120px;margin:auto;padding:20px}"
"header{display:flex;justify-content:space-between;gap:12px;align-items:center}nav a{color:#bae6fd;margin-left:12px}"
".mode,.card,dialog{background:#102037;border:1px solid #294563;border-radius:16px;color:#eef4ff}"
".mode{padding:14px;margin:16px 0;display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
"button,input,select{font:inherit;min-height:44px;border-radius:9px;border:1px solid #3c5877;background:#091629;color:white;padding:0 12px}"
"button{cursor:pointer}.active,.save{background:#0284c7;border-color:#38bdf8}"
".grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}.card{text-align:left;padding:16px;min-height:145px}"
".card strong,.card span{display:block}.card .state{font-size:20px;margin:15px 0 5px}.EMPTY{border-color:#22c55e}"
".OCCUPIED{border-color:#f59e0b}.UNKNOWN{border-color:#64748b}.meta{color:#9eb0c7;font-size:13px}"
"dialog{width:min(580px,calc(100% - 24px));padding:20px}form{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
"label{display:grid;gap:6px;font-size:13px}.full{grid-column:1/-1}.actions{display:flex;justify-content:flex-end;gap:8px}"
".err{color:#fca5a5;min-height:20px}@media(max-width:700px){.grid{grid-template-columns:repeat(2,1fr)}}"
"@media(max-width:420px){.grid,form{grid-template-columns:1fr}.full{grid-column:auto}}</style></head><body>"
"<div class='wrap'><header><div><small>AUBOT GATEWAY</small><h1>Giám sát vị trí kho</h1></div>"
"<nav><a href='/debug'>Chẩn đoán CAN</a><a href='/cau-hinh'>Kết nối</a></nav></header>"
"<section class='mode'><b>Chế độ Laser/Kho:</b><button id='p8'>8 GROUP</button><button id='p12'>12 GROUP</button>"
"<span id='summary' class='meta'></span></section><main id='grid' class='grid'></main></div>"
"<dialog id='dlg'><h2>Cấu hình Group</h2><form id='form'>"
"<label>Group<input id='group' readonly></label><label>LaserID cho phép<input id='range' readonly></label>"
"<label>LaserID<select id='laser'></select></label><label>Mã kho<input id='code' maxlength='15' required></label>"
"<label class='full'>Tên vị trí kho<input id='name' maxlength='31'></label>"
"<label>Distance (mm)<input id='distance' type='number' min='0' max='1200'></label>"
"<label>Distance_E (mm)<input id='emergency' type='number' min='0' max='1200'></label>"
"<label>Row mask<input id='row' type='number' min='0' max='255'></label>"
"<label>Column mask<input id='col' type='number' min='0' max='255'></label>"
"<label><span><input id='enabled' type='checkbox'> Bật vị trí kho</span></label>"
"<label><span><input id='proximity' type='checkbox'> Bật proximity laser</span></label>"
"<div id='runtime' class='meta full'></div><div id='err' class='err full'></div>"
"<div class='actions full'><button type='button' onclick='dlg.close()'>Hủy</button><button class='save'>Lưu cấu hình</button></div>"
"</form></dialog><script>"
"const $=x=>document.getElementById(x);let snap,nodes=[],editing=0;"
"const esc=x=>String(x===undefined||x===null?'':x).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));"
"async function load(){snap=await(await fetch('/api/warehouse/status',{cache:'no-store'})).json();"
"try{nodes=(await(await fetch('/api/debug/status',{cache:'no-store'})).json()).nodes||[]}catch(e){}render();setTimeout(load,1000)}"
"function render(){$('p8').className=snap.profile==='GROUP_8'?'active':'';$('p12').className=snap.profile==='GROUP_12'?'active':'';"
"$('summary').textContent=`${snap.online} online · ${snap.occupied} có hàng · ${snap.unknown} chưa xác định`;"
"$('grid').innerHTML=snap.positions.map(p=>`<button class='card ${p.state}' onclick='openGroup(${p.group_id})'>"
"<strong>GROUP ${p.group_id}</strong><span>${esc(p.warehouse_code||'Chưa cấu hình')}</span>"
"<span class='state'>${p.state==='EMPTY'?'KHÔNG CÓ HÀNG':p.state==='OCCUPIED'?'CÓ HÀNG':'CHƯA XÁC ĐỊNH'}</span>"
"<span class='meta'>LaserID ${p.laser_id||'-'} · ${p.sensor_online?'ONLINE':'OFFLINE'}</span></button>`).join('')}"
"function openGroup(g){editing=g;const p=snap.positions[g-1],a=p.allowed_first,b=p.allowed_last;"
"$('group').value='GROUP '+g;$('range').value=`ID ${a}–${b}`;$('laser').innerHTML='<option value=0>Chưa gán</option>'+"
"Array.from({length:b-a+1},(_,i)=>a+i).map(id=>{const n=nodes.find(x=>x.id===id),used=snap.positions.some(x=>x.group_id!==g&&x.laser_id===id);"
"return `<option value=${id} ${used?'disabled':''}>ID ${id} · ${n&&n.alive?'ONLINE':'CHƯA PHÁT HIỆN'}${used?' · Đã dùng':''}</option>`}).join('');"
"$('laser').value=p.laser_id||0;$('code').value=p.warehouse_code||'';$('name').value=p.warehouse_name||'';"
"$('distance').value=p.config_distance_mm||600;$('emergency').value=p.config_emergency_mm||300;"
"$('row').value=p.high_row||0;$('col').value=p.low_col||0;$('enabled').checked=p.enabled;"
"$('proximity').checked=p.proximity_enabled;$('runtime').textContent=`Trạng thái: ${p.state} · Lần cuối: ${p.last_seen_ago_ms<0?'chưa có':p.last_seen_ago_ms+' ms'} · Cảnh báo: ${p.warn}`;"
"$('err').textContent='';dlg.showModal()}"
"async function profile(p){if(snap.profile===p)return;if(!confirm('Đổi profile sẽ không tự remap. Nếu có mapping xung đột, hệ thống sẽ từ chối. Tiếp tục?'))return;"
"const r=await fetch('/api/warehouse/profile',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({profile:p,clear_conflicts:0})});"
"if(!r.ok)alert('Mapping hiện tại xung đột. Hãy xóa/cấu hình lại trước khi đổi profile.');else load()}"
"$('p8').onclick=()=>profile('GROUP_8');$('p12').onclick=()=>profile('GROUP_12');"
"$('form').onsubmit=async e=>{e.preventDefault();const p={group_id:editing,laser_id:$('laser').value,warehouse_code:$('code').value,"
"warehouse_name:$('name').value,distance_mm:$('distance').value,emergency_mm:$('emergency').value,low_col:$('col').value,"
"high_row:$('row').value,enabled:$('enabled').checked?1:0,proximity:$('proximity').checked?1:0};"
"const r=await fetch('/api/warehouse/position',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(p)});"
"if(!r.ok){$('err').textContent=await r.text();return}dlg.close();load()};load()</script></body></html>";
#endif

static esp_err_t page(httpd_req_t *request)
{
    return gateway_web_send_html(request, GATEWAY_PUBLIC_PAGE);
}

static esp_err_t factory_page(httpd_req_t *request)
{
    if (!gateway_auth_require_page(request, GW_PERMISSION_WAREHOUSE_CONFIG, NULL)) return ESP_OK;
    return gateway_web_send_html(request, GATEWAY_WAREHOUSE_PAGE);
}

static size_t json_escape(char *output, size_t capacity, const char *input)
{
    size_t used = 0;
    for (; *input != '\0' && used + 1U < capacity; ++input) {
        const char *escape = NULL;
        if (*input == '"') escape = "\\\"";
        else if (*input == '\\') escape = "\\\\";
        else if (*input == '\n') escape = "\\n";
        else if (*input == '\r') escape = "\\r";
        else if (*input == '\t') escape = "\\t";
        if (escape != NULL) {
            const size_t length = strlen(escape);
            if (used + length >= capacity) break;
            memcpy(output + used, escape, length);
            used += length;
        } else if ((unsigned char)*input >= 0x20U) {
            output[used++] = *input;
        }
    }
    output[used] = '\0';
    return used;
}

static esp_err_t status(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_WAREHOUSE_CONFIG, NULL)) return ESP_OK;
    warehouse_snapshot_t snapshot;
    warehouse_manager_snapshot(&snapshot);
    char buffer[640];
    int length = snprintf(buffer, sizeof(buffer),
        "{\"profile\":\"%s\",\"group_count\":%u,\"configured\":%u,\"online\":%u,"
        "\"unknown\":%u,\"empty\":%u,\"occupied\":%u,\"positions\":[",
        laser_profile_name(snapshot.profile), snapshot.group_count, snapshot.configured,
        snapshot.online, snapshot.unknown, snapshot.empty, snapshot.occupied);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_send_chunk(request, buffer, length);

    for (uint8_t i = 0; i < snapshot.group_count; ++i) {
        warehouse_position_t *position = &snapshot.positions[i];
        laser_group_definition_t group;
        char code[WAREHOUSE_CODE_LEN * 2U];
        char name[WAREHOUSE_NAME_LEN * 2U];
        json_escape(code, sizeof(code), position->config.warehouse_code);
        json_escape(name, sizeof(name), position->config.warehouse_name);
        laser_profile_group_definition(snapshot.profile, i + 1U, &group);
        length = snprintf(buffer, sizeof(buffer),
            "%s{\"group_id\":%u,\"enabled\":%s,\"proximity_enabled\":%s,\"laser_id\":%u,"
            "\"allowed_first\":%u,\"allowed_last\":%u,\"warehouse_code\":\"%s\",\"warehouse_name\":\"%s\","
            "\"state\":\"%s\",\"sensor_online\":%s,\"last_seen_ago_ms\":%lld,\"warn\":\"%s\","
            "\"config_distance_mm\":%u,\"config_emergency_mm\":%u,\"low_col\":%u,\"high_row\":%u}",
            i ? "," : "", i + 1U, position->config.enabled ? "true" : "false",
            position->config.proximity_enabled ? "true" : "false", position->config.laser_id,
            group.laser_id_first, group.laser_id_last, code, name,
            warehouse_state_name(position->state), position->sensor_online ? "true" : "false",
            position->last_seen_ago_ms, laser_can_obstacle_state_name(position->warn),
            position->config.distance_mm, position->config.distance_emergency_mm,
            position->config.low_col, position->config.high_row);
        httpd_resp_send_chunk(request, buffer, length);
    }
    httpd_resp_send_chunk(request, "]}", 2);
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t public_status(httpd_req_t *request)
{
    warehouse_snapshot_t snapshot;
    warehouse_manager_snapshot(&snapshot);
    gateway_config_t config;
    gateway_config_get(&config);
    bsp_can_status_t can = {0};
    bsp_can_get_status(&can);
    char gateway_id[40], network_name[96];
    json_escape(gateway_id, sizeof(gateway_id), config.gateway_id);
    json_escape(network_name, sizeof(network_name), gateway_network_active_name());
    char buffer[520];
    int length = snprintf(buffer, sizeof(buffer),
        "{\"gateway\":\"%s\",\"profile\":\"%s\",\"online\":%u,\"empty\":%u,"
        "\"occupied\":%u,\"unknown\":%u,\"network\":{\"connected\":%s,\"name\":\"%s\"},"
        "\"mqtt\":%s,\"can\":\"%s\",\"positions\":[",
        gateway_id, laser_profile_name(snapshot.profile), snapshot.online,
        snapshot.empty, snapshot.occupied, snapshot.unknown,
        gateway_network_production_available() ? "true" : "false",
        network_name, gateway_mqtt_is_connected() ? "true" : "false",
        can.state == BSP_CAN_STATE_ACTIVE ? "ACTIVE" :
        can.state == BSP_CAN_STATE_WARNING ? "WARNING" :
        can.state == BSP_CAN_STATE_PASSIVE ? "PASSIVE" :
        can.state == BSP_CAN_STATE_BUS_OFF ? "BUS-OFF" : "STOPPED");
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_send_chunk(request, buffer, length);
    for (uint8_t i = 0; i < snapshot.group_count; ++i) {
        const warehouse_position_t *position = &snapshot.positions[i];
        char code[WAREHOUSE_CODE_LEN * 2U];
        json_escape(code, sizeof(code), position->config.warehouse_code);
        length = snprintf(buffer, sizeof(buffer),
            "%s{\"group_id\":%u,\"laser_id\":%u,\"warehouse_code\":\"%s\","
            "\"state\":\"%s\",\"sensor_online\":%s}", i ? "," : "",
            i + 1U, position->config.laser_id, code,
            warehouse_state_name(position->state), position->sensor_online ? "true" : "false");
        httpd_resp_send_chunk(request, buffer, length);
    }
    httpd_resp_send_chunk(request, "]}", 2U);
    return httpd_resp_send_chunk(request, NULL, 0U);
}

static esp_err_t factory_lasers(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_WAREHOUSE_CONFIG, NULL)) return ESP_OK;
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t error = httpd_resp_send_chunk(request, "{\"nodes\":[", 10U);
    char json[64];
    bool first = true;
    for (uint8_t id = 1U; error == ESP_OK && id <= LASER_CAN_MAX_NODES; ++id) {
        laser_can_node_status_t node = {0};
        if (!laser_can_bringup_get_node(id, &node)) continue;
        const int length = snprintf(json, sizeof(json),
            "%s{\"id\":%u,\"alive\":%s}", first ? "" : ",", id,
            node.alive ? "true" : "false");
        error = httpd_resp_send_chunk(request, json, length);
        first = false;
    }
    if (error == ESP_OK) error = httpd_resp_send_chunk(request, "]}", 2U);
    if (error == ESP_OK) error = httpd_resp_send_chunk(request, NULL, 0U);
    return error;
}

static bool field(const char *body, const char *key, char *output, size_t capacity)
{
    return httpd_query_key_value(body, key, output, capacity) == ESP_OK;
}

static esp_err_t read_body(httpd_req_t *request, char *buffer, size_t capacity)
{
    if (request->content_len == 0U || request->content_len >= capacity) return ESP_ERR_INVALID_SIZE;
    const int received = httpd_req_recv(request, buffer, request->content_len);
    if (received != (int)request->content_len) return ESP_FAIL;
    buffer[received] = '\0';
    return ESP_OK;
}

static esp_err_t send_conflict(httpd_req_t *request, const char *message)
{
    httpd_resp_set_status(request, "409 Conflict");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, message);
}

static esp_err_t set_profile(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_WAREHOUSE_CONFIG, NULL)) return ESP_OK;
    char body[128] = {0}, profile_name[16] = {0}, clear[4] = {0};
    if (read_body(request, body, sizeof(body)) != ESP_OK ||
        !field(body, "profile", profile_name, sizeof(profile_name))) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Dữ liệu không hợp lệ");
    }
    const laser_profile_t profile = strcmp(profile_name, "GROUP_12") == 0
        ? LASER_PROFILE_GROUP_12 : LASER_PROFILE_GROUP_8;
    field(body, "clear_conflicts", clear, sizeof(clear));
    if (warehouse_manager_set_profile(profile, atoi(clear) != 0) != ESP_OK) {
        return send_conflict(request, "Mapping hiện tại xung đột với profile mới");
    }
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t set_position(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_WAREHOUSE_CONFIG, NULL)) return ESP_OK;
    char body[512] = {0}, value[64];
    if (read_body(request, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Dữ liệu quá dài");
    }
    warehouse_position_config_t position = {0};
#define READ_UINT(key, member) do { \
    if (!field(body, key, value, sizeof(value))) \
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Thiếu trường " key); \
    position.member = (typeof(position.member))strtoul(value, NULL, 10); \
} while (0)
    READ_UINT("group_id", group_id);
    READ_UINT("laser_id", laser_id);
    READ_UINT("distance_mm", distance_mm);
    READ_UINT("emergency_mm", distance_emergency_mm);
    READ_UINT("low_col", low_col);
    READ_UINT("high_row", high_row);
    READ_UINT("enabled", enabled);
    READ_UINT("proximity", proximity_enabled);
#undef READ_UINT
    if (field(body, "warehouse_code", value, sizeof(value)))
        strlcpy(position.warehouse_code, value, sizeof(position.warehouse_code));
    if (field(body, "warehouse_name", value, sizeof(value)))
        strlcpy(position.warehouse_name, value, sizeof(position.warehouse_name));

    const warehouse_validation_t validation = warehouse_manager_validate_position(&position);
    if (validation != WAREHOUSE_VALID) {
        return send_conflict(request, warehouse_validation_name(validation));
    }
    if (warehouse_manager_set_position(&position) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Không thể lưu NVS");
    }
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t apply_laser(httpd_req_t *request)
{
    if (!gateway_auth_require_api(request, GW_PERMISSION_LASER_CONFIG, NULL)) return ESP_OK;
    char body[64] = {0}, value[8] = {0};
    if (read_body(request, body, sizeof(body)) != ESP_OK ||
        !field(body, "group_id", value, sizeof(value)))
        return httpd_resp_send_err(request, 400, "Thiếu Group");
    warehouse_position_t position;
    if (!warehouse_manager_get_position((uint8_t)atoi(value), &position) ||
        !position.config.enabled)
        return httpd_resp_send_err(request, 409, "Vị trí chưa được bật");
    const laser_can_config_request_t config = {
        .laser_id = position.config.laser_id,
        .distance_mm = position.config.distance_mm,
        .distance_emergency_mm = position.config.distance_emergency_mm,
        .low_col = position.config.low_col,
        .high_row = position.config.high_row,
        .proximity_enabled = position.config.proximity_enabled,
    };
    const esp_err_t error = laser_can_bringup_configure(&config, NULL);
    if (error != ESP_OK) return httpd_resp_send_err(request, 500, esp_err_to_name(error));
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

esp_err_t warehouse_http_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/app/factory", .method = HTTP_GET, .handler = factory_page},
        {.uri = "/api/warehouse/status", .method = HTTP_GET, .handler = status},
        {.uri = "/api/public/status", .method = HTTP_GET, .handler = public_status},
        {.uri = "/api/warehouse/profile", .method = HTTP_POST, .handler = set_profile},
        {.uri = "/api/warehouse/position", .method = HTTP_POST, .handler = set_position},
        {.uri = "/api/factory/status", .method = HTTP_GET, .handler = status},
        {.uri = "/api/factory/lasers", .method = HTTP_GET, .handler = factory_lasers},
        {.uri = "/api/factory/profile", .method = HTTP_POST, .handler = set_profile},
        {.uri = "/api/factory/warehouse", .method = HTTP_POST, .handler = set_position},
        {.uri = "/api/factory/laser/apply", .method = HTTP_POST, .handler = apply_laser},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const esp_err_t error = httpd_register_uri_handler(server, &handlers[i]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
