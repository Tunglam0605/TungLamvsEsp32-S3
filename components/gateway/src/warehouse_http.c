#include "warehouse_http.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "laser_can_bringup.h"
#include "warehouse_manager.h"

static const char WAREHOUSE_PAGE[] =
    "<!doctype html><html lang='vi'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta name='color-scheme' content='dark'><title>AUBOT | Quản lý kho Laser</title><style>"
    ":root{--bg:#0b1220;--surface:#172033;--surface2:#111b2d;--border:#334155;--text:#f8fafc;--muted:#94a3b8;--blue:#38bdf8;--green:#22c55e;--amber:#fbbf24;--red:#ef4444;--radius:15px}"
    "*{box-sizing:border-box}html{font-family:Inter,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text)}body{margin:0;min-height:100vh;background:radial-gradient(circle at 80% -20%,#172554 0,transparent 38%),var(--bg)}button,input,select{font:inherit}:focus-visible{outline:3px solid var(--blue);outline-offset:2px}.shell{width:min(1240px,100%);margin:auto;padding:22px}.skip{position:absolute;left:-9999px}.skip:focus{left:16px;top:16px;background:#fff;color:#111;padding:10px;z-index:9}"
    ".top{display:flex;justify-content:space-between;align-items:center;gap:16px;margin-bottom:18px}.brand{display:flex;align-items:center;gap:12px}.mark{width:42px;height:42px;display:grid;place-items:center;border:1px solid #2563eb;border-radius:12px;background:#172554;color:#7dd3fc}.mark svg{width:23px}.kicker{font-size:10px;letter-spacing:.15em;color:#7dd3fc;font-weight:800}.title{font-size:clamp(21px,3vw,29px);margin:3px 0 0}.nav{display:flex;gap:8px}.nav a{min-height:44px;display:inline-flex;align-items:center;padding:0 14px;border:1px solid var(--border);border-radius:10px;color:#cbd5e1;text-decoration:none;background:var(--surface)}"
    ".hero{display:grid;grid-template-columns:1.4fr .6fr;gap:14px;margin-bottom:14px}.panel,.metric,.slot{border:1px solid var(--border);background:linear-gradient(145deg,#1b263b,#172033);border-radius:var(--radius)}.hero-main{padding:22px}.live{display:flex;align-items:center;gap:8px;color:#86efac;font-size:12px;font-weight:750}.dot{width:8px;height:8px;border-radius:50%;background:currentColor}.hero h2{font-size:25px;margin:13px 0 7px}.muted{color:var(--muted);font-size:13px;line-height:1.5}.hero-side{padding:18px;display:grid;align-content:center;gap:10px}.hero-row{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid var(--border);font-size:13px}.hero-row:last-child{border:0}.hero-row span{color:var(--muted)}"
    ".metrics{display:grid;grid-template-columns:repeat(5,1fr);gap:10px;margin-bottom:14px}.metric{padding:15px;min-width:0}.metric-label{font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:.09em;font-weight:800}.metric-value{font:700 26px ui-monospace,Consolas,monospace;margin-top:9px}.green{color:#86efac}.amber{color:#fde68a}.red{color:#fca5a5}"
    ".workspace{display:grid;grid-template-columns:minmax(0,1fr) 330px;gap:14px}.board{padding:18px;min-width:0}.panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}.panel-head h2{font-size:16px;margin:0}.filter{min-height:40px;border:1px solid var(--border);border-radius:9px;background:var(--surface2);color:var(--text);padding:0 10px}.slots{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}.slot{padding:14px;box-shadow:none;position:relative;overflow:hidden}.slot:before{content:'';position:absolute;left:0;top:0;bottom:0;width:4px;background:#64748b}.slot.empty:before{background:var(--green)}.slot.occupied:before{background:var(--amber)}.slot.critical:before{background:var(--red)}.slot-code{font:750 18px ui-monospace,Consolas,monospace}.slot-name{color:var(--muted);font-size:12px;min-height:18px;margin-top:3px}.slot-state{display:inline-flex;margin-top:12px;padding:5px 8px;border-radius:999px;background:#29364c;color:#cbd5e1;font-size:10px;font-weight:800}.slot.empty .slot-state{background:#123524;color:#86efac}.slot.occupied .slot-state{background:#3b2b0d;color:#fde68a}.slot.critical .slot-state{background:#451a1a;color:#fca5a5}.slot-meta{display:flex;justify-content:space-between;gap:8px;margin-top:11px;color:var(--muted);font-size:11px}.slot-edit{width:100%;min-height:38px;margin-top:11px;border:1px solid var(--border);border-radius:8px;background:#22304a;color:var(--text);cursor:pointer}.empty-board{grid-column:1/-1;padding:44px 20px;text-align:center;color:var(--muted);border:1px dashed var(--border);border-radius:12px}"
    ".setup{padding:18px;height:max-content;position:sticky;top:14px}.form{display:grid;gap:12px}.field{display:grid;gap:6px}.field label{font-size:12px;color:#cbd5e1;font-weight:700}.input{width:100%;min-height:44px;border:1px solid #475569;border-radius:9px;background:var(--surface2);color:var(--text);padding:0 11px}.form-actions{display:grid;grid-template-columns:1fr auto;gap:8px}.primary,.danger{min-height:44px;border-radius:9px;font-weight:800;cursor:pointer}.primary{border:0;background:#0284c7;color:#fff}.danger{border:1px solid #7f1d1d;background:#351719;color:#fca5a5;padding:0 12px}.danger:disabled,.primary:disabled{opacity:.45;cursor:not-allowed}.feedback{min-height:38px;padding:10px;border-radius:9px;background:#121c2e;color:var(--muted);font-size:11px;line-height:1.45}.feedback.ok{color:#86efac;border:1px solid #166534}.feedback.bad{color:#fca5a5;border:1px solid #991b1b}.unassigned{margin-top:14px;padding-top:13px;border-top:1px solid var(--border)}.unassigned-list{display:flex;flex-wrap:wrap;gap:7px;margin-top:8px}.sensor-chip{border:1px solid var(--border);border-radius:8px;background:#202c43;color:#cbd5e1;min-height:36px;padding:0 9px;cursor:pointer}.sensor-chip.off{color:#fca5a5}"
    ".footer{display:flex;justify-content:space-between;gap:10px;flex-wrap:wrap;margin-top:14px;color:var(--muted);font-size:11px;padding:0 3px}@media(max-width:980px){.metrics{grid-template-columns:repeat(3,1fr)}.workspace{grid-template-columns:1fr}.setup{position:static}.slots{grid-template-columns:repeat(2,1fr)}}@media(max-width:680px){.shell{padding:14px}.hero{grid-template-columns:1fr}.metrics{grid-template-columns:repeat(2,1fr)}.slots{grid-template-columns:1fr}.nav a{padding:0 10px}.hero-main{padding:18px}}@media(max-width:400px){.metrics{grid-template-columns:1fr}.top{align-items:flex-start}.kicker{display:none}}@media(prefers-reduced-motion:reduce){*{transition:none!important}}"
    "</style></head><body><a class='skip' href='#main'>Bỏ qua tới nội dung chính</a><div class='shell'>"
    "<header class='top'><div class='brand'><div class='mark' aria-hidden='true'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='1.8'><path d='M3 8 12 3l9 5-9 5-9-5Z'/><path d='m3 12 9 5 9-5M3 16l9 5 9-5'/></svg></div><div><div class='kicker'>AUBOT WAREHOUSE GATEWAY</div><h1 class='title'>Giám sát vị trí kho</h1></div></div><nav class='nav'><a href='/debug'>Chẩn đoán CAN</a><a href='/cau-hinh'>Cấu hình mạng</a></nav></header>"
    "<main id='main'><section class='hero'><article class='panel hero-main'><div class='live'><span class='dot'></span><span id='liveText'>Đang kết nối Gateway</span></div><h2>Kho hàng theo thời gian thực</h2><p class='muted'>Mỗi Laser quản lý một ô kho. Trạng thái được xác định trực tiếp từ heartbeat riêng của từng LaserID.</p></article><aside class='panel hero-side'><div class='hero-row'><span>Laser phát hiện</span><strong id='sensorCount'>0</strong></div><div class='hero-row'><span>Ô chưa được gán</span><strong id='unassignedCount'>0</strong></div><div class='hero-row'><span>Cập nhật gần nhất</span><strong id='updated'>--</strong></div></aside></section>"
    "<section class='metrics' aria-label='Tổng quan kho'><article class='metric'><div class='metric-label'>Ô đã cấu hình</div><div id='assigned' class='metric-value'>0</div></article><article class='metric'><div class='metric-label'>Đang trực tuyến</div><div id='online' class='metric-value green'>0</div></article><article class='metric'><div class='metric-label'>Ô trống</div><div id='empty' class='metric-value green'>0</div></article><article class='metric'><div class='metric-label'>Có hàng</div><div id='occupied' class='metric-value amber'>0</div></article><article class='metric'><div class='metric-label'>Mất kết nối</div><div id='offline' class='metric-value red'>0</div></article></section>"
    "<section class='workspace'><article class='panel board'><div class='panel-head'><h2>Sơ đồ ô kho</h2><select id='clusterFilter' class='filter' aria-label='Lọc theo cụm kho'><option value='0'>Tất cả cụm kho</option></select></div><div id='slots' class='slots'><div class='empty-board'>Chưa có ô kho được cấu hình.</div></div></article>"
    "<aside class='panel setup'><div class='panel-head'><h2>Phân ô và cụm kho</h2><span id='editMode' class='muted'>THÊM MỚI</span></div><form id='mapForm' class='form'><div class='field'><label for='laserSelect'>LaserID</label><select id='laserSelect' class='input' required><option value=''>Chưa phát hiện Laser</option></select></div><div class='field'><label for='slotIndex'>Thứ tự ô MQTT (1–64)</label><input id='slotIndex' class='input' type='number' min='1' max='64' required></div><div class='field'><label for='slotCode'>Mã ô kho</label><input id='slotCode' class='input' maxlength='15' placeholder='Ví dụ: A-01-01' required></div><div class='field'><label for='slotName'>Tên vị trí</label><input id='slotName' class='input' maxlength='31' placeholder='Ví dụ: Kệ A · Tầng 1'></div><div class='field'><label for='clusterId'>Cụm kho</label><select id='clusterId' class='input'></select></div><div class='form-actions'><button id='saveMap' class='primary' type='submit'>Lưu ánh xạ</button><button id='clearMap' class='danger' type='button' disabled>Bỏ gán</button></div><div id='feedback' class='feedback' role='status' aria-live='polite'>Chọn Laser rồi nhập mã ô kho.</div></form><div class='unassigned'><strong>Laser chưa phân ô</strong><div id='unassigned' class='unassigned-list'><span class='muted'>Chưa có dữ liệu.</span></div></div></aside></section></main>"
    "<footer class='footer'><span>Ánh xạ được lưu trong bộ nhớ Gateway</span><span>ESP32-S3 · Ethernet W5500 · CAN 250 kbit/s</span></footer></div>"
    "<script>const $=id=>document.getElementById(id),set=(id,v)=>$(id).textContent=v;let model={slots:[],sensors:[],summary:{}},selected=0;const stateText={OFFLINE:'MẤT KẾT NỐI',WAITING:'ĐANG CHỜ DỮ LIỆU',EMPTY:'KHÔNG CÓ HÀNG',OCCUPIED:'CÓ HÀNG',CRITICAL:'CÓ HÀNG'};"
    "function stateClass(s){return s==='EMPTY'?'empty':s==='OCCUPIED'||s==='CRITICAL'?'occupied':'offline'}function clusterOptions(){let h='';for(let i=1;i<=16;i++)h+='<option value='+i+'>Cụm '+i+'</option>';$('clusterId').innerHTML=h}"
    "function selectLaser(id){selected=Number(id);$('laserSelect').value=String(selected);const slot=model.slots.find(s=>s.laser_id===selected);$('slotIndex').value=slot?slot.slot_index:selected;if(slot){$('slotCode').value=slot.slot_code;$('slotName').value=slot.slot_name;$('clusterId').value=slot.cluster_id;$('clearMap').disabled=false;set('editMode','ĐANG SỬA')}else{$('slotCode').value='';$('slotName').value='';$('clearMap').disabled=true;set('editMode','THÊM MỚI')}set('feedback',slot?'Đang chỉnh sửa ánh xạ đã lưu.':'Nhập vị trí kho cho Laser '+selected);$('feedback').className='feedback'}"
    "function renderSlots(){const filter=Number($('clusterFilter').value),slots=model.slots.filter(s=>!filter||s.cluster_id===filter);$('slots').innerHTML=slots.length?slots.map(s=>`<article class='slot ${stateClass(s.state)}'><div class='slot-code'>${esc(s.slot_code)}</div><div class='slot-name'>${esc(s.slot_name||'Chưa đặt tên')}</div><span class='slot-state'>${stateText[s.state]}</span><div class='slot-meta'><span>Laser ${s.laser_id}</span><span>Cụm ${s.cluster_id} · CAN ${s.b300_group||'--'}</span></div><button class='slot-edit' type='button' data-id='${s.laser_id}'>Thiết lập</button></article>`).join(''):`<div class='empty-board'>Không có ô kho trong bộ lọc này.</div>`;$('slots').querySelectorAll('button').forEach(b=>b.onclick=()=>selectLaser(b.dataset.id))}"
    "function esc(v){return String(v||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}function render(d){model=d;const s=d.summary;set('assigned',s.assigned);set('online',s.online);set('empty',s.empty);set('occupied',s.occupied+s.critical);set('offline',s.offline);set('sensorCount',d.sensors.length);const unmapped=d.sensors.filter(x=>!x.assigned);set('unassignedCount',unmapped.length);set('liveText','Gateway trực tuyến · '+d.sensors.filter(x=>x.online).length+' Laser đang hoạt động');set('updated',new Date().toLocaleTimeString('vi-VN'));const ids=[...new Set([...d.sensors.map(x=>x.laser_id),...d.slots.map(x=>x.laser_id)])].sort((a,b)=>a-b);$('laserSelect').innerHTML=ids.length?ids.map(id=>`<option value='${id}'>Laser ${id}</option>`).join(''):`<option value=''>Chưa phát hiện Laser</option>`;const clusters=[...new Set(d.slots.map(x=>x.cluster_id))].sort((a,b)=>a-b),old=$('clusterFilter').value;$('clusterFilter').innerHTML=`<option value='0'>Tất cả cụm kho</option>`+clusters.map(c=>`<option value='${c}'>Cụm ${c}</option>`).join('');$('clusterFilter').value=[...$('clusterFilter').options].some(o=>o.value===old)?old:'0';$('unassigned').innerHTML=unmapped.length?unmapped.map(x=>`<button class='sensor-chip ${x.online?'':'off'}' type='button' data-id='${x.laser_id}'>Laser ${x.laser_id}</button>`).join(''):`<span class='muted'>Tất cả Laser đã được phân ô.</span>`;$('unassigned').querySelectorAll('button').forEach(b=>b.onclick=()=>selectLaser(b.dataset.id));renderSlots();if(!selected&&ids.length)selectLaser(ids[0])}"
    "async function poll(){try{const r=await fetch('/api/warehouse/status',{cache:'no-store'});if(!r.ok)throw Error(r.status);render(await r.json())}catch(e){set('liveText','Mất kết nối với Gateway')}setTimeout(poll,750)}"
    "async function save(clear){if(!selected)return;const body=new URLSearchParams({laser_id:selected,assigned:clear?0:1,slot_index:$('slotIndex').value,cluster_id:$('clusterId').value,slot_code:$('slotCode').value,slot_name:$('slotName').value});$('saveMap').disabled=true;set('feedback',clear?'Đang bỏ ánh xạ...':'Đang lưu ánh xạ...');try{const r=await fetch('/api/warehouse/map',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok)throw Error(await r.text());set('feedback',clear?'Đã bỏ gán Laser khỏi ô kho.':'Đã lưu ô kho và cụm kho vào Gateway.');$('feedback').className='feedback ok';await poll()}catch(e){set('feedback','Không thể lưu: '+e.message);$('feedback').className='feedback bad'}finally{$('saveMap').disabled=false}}"
    "clusterOptions();$('clusterFilter').onchange=renderSlots;$('laserSelect').onchange=e=>selectLaser(e.target.value);$('mapForm').onsubmit=e=>{e.preventDefault();save(false)};$('clearMap').onclick=()=>{if(confirm('Bỏ ánh xạ ô kho của Laser '+selected+'?'))save(true)};poll();</script></body></html>";

static esp_err_t page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, WAREHOUSE_PAGE, HTTPD_RESP_USE_STRLEN);
}

static void json_escape(const char *src, char *dst, size_t size)
{
    size_t out = 0U;
    for (size_t i = 0U; src[i] != '\0' && out + 2U < size; ++i) {
        if (src[i] == '"' || src[i] == '\\') dst[out++] = '\\';
        dst[out++] = src[i];
    }
    dst[out] = '\0';
}

static esp_err_t status_handler(httpd_req_t *req)
{
    warehouse_slot_status_t slots[LASER_CAN_MAX_NODES];
    warehouse_summary_t summary = { 0 };
    const size_t slot_count = warehouse_manager_get_slots(
        slots, LASER_CAN_MAX_NODES, &summary);
    char json[512];
    int len = snprintf(json, sizeof(json),
        "{\"summary\":{\"assigned\":%u,\"online\":%u,\"offline\":%u,"
        "\"empty\":%u,\"occupied\":%u,\"critical\":%u},\"slots\":[",
        summary.assigned, summary.online, summary.offline, summary.empty,
        summary.occupied, summary.critical);
    if (len < 0 || (size_t)len >= sizeof(json)) return ESP_ERR_INVALID_SIZE;
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send_chunk(req, json, len);

    for (size_t i = 0U; err == ESP_OK && i < slot_count; ++i) {
        char code[(WAREHOUSE_SLOT_CODE_LEN * 2U) + 1U];
        char name[(WAREHOUSE_SLOT_NAME_LEN * 2U) + 1U];
        json_escape(slots[i].mapping.slot_code, code, sizeof(code));
        json_escape(slots[i].mapping.slot_name, name, sizeof(name));
        len = snprintf(json, sizeof(json),
            "%s{\"laser_id\":%u,\"slot_index\":%u,\"cluster_id\":%u,\"slot_code\":\"%s\"," 
            "\"slot_name\":\"%s\",\"b300_group\":%u,\"online\":%s,"
            "\"state\":\"%s\",\"last_seen_ms\":%" PRId64 ","
            "\"enabled\":%s,\"distance_mm\":%u,\"emergency_mm\":%u}",
            i == 0U ? "" : ",", slots[i].mapping.laser_id,
            slots[i].mapping.slot_index, slots[i].mapping.cluster_id, code, name, slots[i].b300_group,
            slots[i].sensor_online ? "true" : "false",
            warehouse_slot_state_name(slots[i].state), slots[i].last_seen_ms,
            slots[i].proximity_enabled ? "true" : "false",
            slots[i].distance_mm, slots[i].distance_emergency_mm);
        if (len < 0 || (size_t)len >= sizeof(json)) return ESP_ERR_INVALID_SIZE;
        err = httpd_resp_send_chunk(req, json, len);
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, "],\"sensors\":[", 13U);

    laser_can_node_status_t nodes[LASER_CAN_MAX_NODES];
    const size_t node_count = laser_can_bringup_get_nodes(nodes, LASER_CAN_MAX_NODES);
    for (size_t i = 0U; err == ESP_OK && i < node_count; ++i) {
        warehouse_mapping_t mapping = { 0 };
        const bool assigned = warehouse_manager_get_mapping(nodes[i].laser_id, &mapping);
        len = snprintf(json, sizeof(json),
            "%s{\"laser_id\":%u,\"b300_group\":%u,\"online\":%s,"
            "\"assigned\":%s,\"obstacle\":\"%s\"}",
            i == 0U ? "" : ",", nodes[i].laser_id,
            (unsigned)(nodes[i].group + 1U), nodes[i].alive ? "true" : "false",
            assigned ? "true" : "false",
            laser_can_obstacle_state_name(nodes[i].obstacle_state));
        if (len < 0 || (size_t)len >= sizeof(json)) return ESP_ERR_INVALID_SIZE;
        err = httpd_resp_send_chunk(req, json, len);
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, "]}", 2U);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0U);
    return err;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool url_decode(char *text)
{
    char *read = text;
    char *write = text;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            ++read;
        } else if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
            const int high = hex_value(read[1]);
            const int low = hex_value(read[2]);
            if (high < 0 || low < 0) return false;
            *write++ = (char)((high << 4) | low);
            read += 3;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
    return true;
}

static bool form_value(const char *body, const char *key, char *value, size_t size)
{
    if (httpd_query_key_value(body, key, value, size) != ESP_OK) return false;
    return url_decode(value);
}

static esp_err_t mapping_handler(httpd_req_t *req)
{
    if (req->content_len == 0U || req->content_len >= 256U) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dữ liệu không hợp lệ");
    }
    char body[256] = { 0 };
    size_t received = 0U;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                               "Không nhận đủ dữ liệu");
        received += (size_t)n;
    }
    char laser_text[8], assigned_text[4];
    if (!form_value(body, "laser_id", laser_text, sizeof(laser_text)) ||
        !form_value(body, "assigned", assigned_text, sizeof(assigned_text))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Thiếu LaserID");
    }
    const unsigned long laser_id = strtoul(laser_text, NULL, 10);
    if (laser_id == 0UL || laser_id > LASER_CAN_MAX_NODES) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LaserID ngoài phạm vi");
    }
    esp_err_t err;
    if (assigned_text[0] == '0') {
        err = warehouse_manager_clear_mapping((uint8_t)laser_id);
    } else {
        char cluster_text[8], slot_index_text[8] = { 0 };
        char slot_code[WAREHOUSE_SLOT_CODE_LEN];
        char slot_name[WAREHOUSE_SLOT_NAME_LEN];
        if (!form_value(body, "cluster_id", cluster_text, sizeof(cluster_text)) ||
            !form_value(body, "slot_code", slot_code, sizeof(slot_code)) ||
            !form_value(body, "slot_name", slot_name, sizeof(slot_name))) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "Thiếu thông tin ô kho");
        }
        const unsigned long cluster = strtoul(cluster_text, NULL, 10);
        const bool has_slot_index = form_value(body, "slot_index", slot_index_text,
                                               sizeof(slot_index_text));
        const unsigned long slot_index = has_slot_index
                                             ? strtoul(slot_index_text, NULL, 10)
                                             : laser_id;
        warehouse_mapping_t mapping = {
            .assigned = true,
            .laser_id = (uint8_t)laser_id,
            .slot_index = (uint8_t)slot_index,
            .cluster_id = (uint8_t)cluster,
        };
        snprintf(mapping.slot_code, sizeof(mapping.slot_code), "%s", slot_code);
        snprintf(mapping.slot_name, sizeof(mapping.slot_name), "%s", slot_name);
        err = warehouse_manager_set_mapping(&mapping);
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Không thể lưu ánh xạ ô kho");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t warehouse_http_register(httpd_handle_t server)
{
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = page_handler
    };
    const httpd_uri_t page = {
        .uri = "/warehouse", .method = HTTP_GET, .handler = page_handler
    };
    const httpd_uri_t status = {
        .uri = "/api/warehouse/status", .method = HTTP_GET, .handler = status_handler
    };
    const httpd_uri_t mapping = {
        .uri = "/api/warehouse/map", .method = HTTP_POST, .handler = mapping_handler
    };
    esp_err_t err = httpd_register_uri_handler(server, &root);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &page);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &status);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &mapping);
    return err;
}
