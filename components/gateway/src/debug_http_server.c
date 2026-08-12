#include "debug_http_server.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_can.h"
#include "bsp_eth.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "laser_can_bringup.h"
#include "warehouse_http.h"
#include "gateway_config_http.h"
#include "gateway_debug_page.h"
#include "gateway_web_theme.h"
#include "gateway_auth.h"
#include "gateway_auth_http.h"
#include "gateway_admin_http.h"

static const char *TAG = "DEBUG_HTTP";
static httpd_handle_t s_server;

#if 0
static const char DEBUG_PAGE[] =
    "<!doctype html><html lang='vi'><head>"
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta name='color-scheme' content='dark'><title>AUBOT | Giám sát Laser CAN</title>"
    "<style>"
    ":root{--bg:#0f172a;--surface:#172033;--surface2:#1b263b;--border:#334155;--border2:#475569;"
    "--text:#f8fafc;--muted:#94a3b8;--green:#22c55e;--green-bg:#123524;--blue:#38bdf8;"
    "--amber:#fbbf24;--red:#ef4444;--r:16px;--shadow:0 18px 50px rgba(2,6,23,.28)}"
    "*{box-sizing:border-box}html{background:var(--bg);color:var(--text);font-family:'Fira Sans',Inter,ui-sans-serif,"
    "system-ui,-apple-system,'Segoe UI',sans-serif}body{margin:0;min-height:100dvh;background:linear-gradient(135deg,#111b30 0,#0f172a 45%,#0b1220 100%)}"
    "button,a{font:inherit}.skip{position:absolute;left:-9999px}.skip:focus{left:16px;top:16px;z-index:20;background:#fff;color:#0f172a;padding:12px 16px;border-radius:8px}"
    ":focus-visible{outline:3px solid var(--blue);outline-offset:3px}.shell{width:min(1180px,100%);margin:auto;padding:24px}"
    ".topbar{display:flex;align-items:center;justify-content:space-between;gap:20px;margin-bottom:20px}.brand{display:flex;align-items:center;gap:13px}"
    ".mark{display:grid;place-items:center;width:42px;height:42px;border:1px solid #3b82f6;border-radius:12px;background:#172554;color:#7dd3fc}"
    ".mark svg{width:23px;height:23px}.kicker{color:var(--blue);font-size:11px;font-weight:800;letter-spacing:.16em}.title{margin:3px 0 0;font-size:clamp(21px,3vw,28px);line-height:1.2}"
    ".actions{display:flex;align-items:center;gap:10px}.btn,.api{min-height:44px;display:inline-flex;align-items:center;justify-content:center;gap:8px;border-radius:10px;padding:0 14px}"
    ".btn{color:var(--text);background:var(--surface);border:1px solid var(--border2);cursor:pointer}.btn:hover{background:#243047}.btn svg,.api svg{width:16px;height:16px}"
    ".api{color:#cbd5e1;text-decoration:none;border:1px solid transparent}.api:hover{color:#fff;background:rgba(51,65,85,.5)}"
    ".hero{display:grid;grid-template-columns:minmax(0,1.5fr) minmax(250px,.5fr);gap:16px;margin-bottom:16px}.panel,.kpi{background:linear-gradient(145deg,rgba(27,38,59,.98),rgba(23,32,51,.98));border:1px solid var(--border);border-radius:var(--r);box-shadow:var(--shadow)}"
    ".laser-panel{padding:26px;display:flex;align-items:center;justify-content:space-between;gap:24px;min-height:190px;overflow:hidden;position:relative}"
    ".laser-panel:after{content:'';position:absolute;width:220px;height:220px;right:-90px;top:-100px;border-radius:50%;background:rgba(34,197,94,.08);pointer-events:none}"
    ".statusline{display:flex;align-items:center;gap:9px;color:#bbf7d0;font-size:13px;font-weight:750}.statusline.off{color:#fecaca}.dot{width:9px;height:9px;border-radius:50%;background:currentColor;box-shadow:0 0 0 5px rgba(34,197,94,.12)}"
    ".statusline.off .dot{box-shadow:0 0 0 5px rgba(239,68,68,.12)}.laser-title{font-size:clamp(27px,5vw,44px);margin:14px 0 7px;line-height:1.05}.muted{color:var(--muted);font-size:14px;line-height:1.5}"
    ".id-block{position:relative;z-index:1;min-width:132px;text-align:center;padding:17px 22px;border:1px solid #38604b;background:rgba(18,53,36,.72);border-radius:14px}.id-label,.label{color:var(--muted);font-size:11px;font-weight:800;letter-spacing:.1em;text-transform:uppercase}"
    ".laser-id{font-family:'Fira Code',ui-monospace,Consolas,monospace;font-size:58px;line-height:1.05;font-weight:750;color:#86efac;margin-top:5px;font-variant-numeric:tabular-nums}.laser-id.empty{color:var(--muted)}"
    ".health{padding:20px;display:flex;flex-direction:column;justify-content:center;gap:12px}.health-row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 0;border-bottom:1px solid var(--border)}.health-row:last-child{border-bottom:0}"
    ".health-name{display:flex;align-items:center;gap:10px;color:#cbd5e1;font-size:14px}.health-name svg{width:18px;height:18px;color:var(--muted)}.chip{padding:6px 9px;border-radius:999px;background:#29364c;color:#cbd5e1;font-size:11px;font-weight:800;letter-spacing:.04em}.chip.ok{background:var(--green-bg);color:#86efac}.chip.bad{background:#451a1a;color:#fca5a5}"
    ".kpis{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-bottom:16px}.kpi{padding:18px;min-width:0;min-height:126px;overflow:hidden;box-shadow:none}.kpi-top{display:flex;justify-content:space-between;align-items:center;gap:10px}.kpi-icon{width:34px;height:34px;display:grid;place-items:center;flex:0 0 auto;border-radius:9px;background:#22304a;color:#7dd3fc}.kpi-icon svg{width:17px;height:17px}"
    ".value{font-family:'Fira Code',ui-monospace,Consolas,monospace;font-variant-numeric:tabular-nums;font-size:28px;font-weight:700;margin-top:13px}.sub{color:var(--muted);font-size:12px;margin-top:5px}.good{color:#86efac}.badtext{color:#fca5a5}.warn{color:#fde68a}"
    ".details{display:grid;grid-template-columns:1fr 1fr;gap:16px}.detail{padding:20px}.panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:15px}.panel-title{font-size:16px;margin:0}.type{color:#bae6fd;background:#0c4a6e;padding:5px 8px;border-radius:7px;font-size:11px;font-weight:800}"
    ".frame{display:grid;grid-template-columns:repeat(3,1fr);border:1px solid var(--border);border-radius:12px;overflow:hidden}.frame-cell{padding:15px;border-right:1px solid var(--border)}.frame-cell:last-child{border:0}.frame-value{font-family:'Fira Code',ui-monospace,Consolas,monospace;font-size:19px;font-weight:700;margin-top:7px}"
    ".trend{height:46px;margin-top:14px}.trend svg{width:100%;height:100%;overflow:visible}.trend-line{fill:none;stroke:var(--blue);stroke-width:2;vector-effect:non-scaling-stroke}.trend-base{stroke:var(--border);stroke-width:1;vector-effect:non-scaling-stroke}"
    ".rows{border-top:1px solid var(--border)}.row{display:flex;justify-content:space-between;align-items:center;gap:16px;min-height:44px;border-bottom:1px solid var(--border);font-size:13px}.row:last-child{border-bottom:0}.key{color:var(--muted)}.mono{font-family:'Fira Code',ui-monospace,Consolas,monospace;font-variant-numeric:tabular-nums}"
    ".error-summary{padding:13px 14px;margin-bottom:12px;border:1px solid var(--border);border-radius:11px;background:#121c2e}.error-summary.warn-state{border-color:#7c5d20;background:#302614}.error-summary.bad-state{border-color:#991b1b;background:#351719}.error-kind{display:block;font-size:15px;color:#86efac}.warn-state .error-kind{color:#fde68a}.bad-state .error-kind{color:#fca5a5}.error-help{display:block;margin-top:5px;color:#cbd5e1;font-size:11px;line-height:1.45}.error-grid{display:grid;grid-template-columns:1fr 1fr;border:1px solid var(--border);border-radius:11px;overflow:hidden}.error-item{display:flex;justify-content:space-between;gap:10px;padding:11px 12px;border-bottom:1px solid var(--border);font-size:12px}.error-item:nth-child(odd){border-right:1px solid var(--border)}.error-item:nth-last-child(-n+2){border-bottom:0}.error-item span{color:var(--muted)}"
    ".manage{display:grid;grid-template-columns:minmax(0,1.35fr) minmax(320px,.65fr);gap:16px;margin-top:16px}.manage-card{padding:20px;min-width:0}.count{color:#bae6fd;font-size:12px;font-weight:800}.mini-btn{min-height:44px;border:1px solid var(--border2);border-radius:8px;padding:0 10px;background:#22304a;color:var(--text);cursor:pointer;transition:background .2s,border-color .2s,color .2s}.mini-btn:hover{background:#2b3b57}.empty{padding:28px;text-align:center;color:var(--muted)}"
    ".id-grid-wrap{overflow-x:auto;padding:2px 2px 8px}.id-grid{display:grid;grid-template-columns:58px repeat(8,minmax(48px,1fr));gap:8px;min-width:520px}.group-pick,.laser-cell{position:relative;min-height:58px;border-radius:10px;cursor:pointer;transition:background .2s,border-color .2s,box-shadow .2s,color .2s}.group-pick{border:1px solid var(--border2);background:#19263b;color:#cbd5e1;font-size:11px;font-weight:800}.group-pick:hover,.group-pick.selected{border-color:var(--blue);background:#0c4a6e;color:#e0f2fe}.group-pick small{display:block;margin-top:3px;color:var(--muted);font-size:9px}.laser-cell{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;border:1px solid #2d3b51;background:#111827;color:#64748b;font:700 16px 'Fira Code',ui-monospace,monospace}.laser-cell small{max-width:90%;overflow:hidden;text-overflow:ellipsis;color:currentColor;opacity:.72;font:500 9px 'Fira Sans',sans-serif;white-space:nowrap}.laser-cell:before{content:'';position:absolute;top:7px;right:7px;width:7px;height:7px;border-radius:50%;background:#475569}.laser-cell.online{border-color:#23834b;background:#123524;color:#bbf7d0;box-shadow:inset 0 0 0 1px rgba(34,197,94,.12),0 0 14px rgba(34,197,94,.13)}.laser-cell.online:before{background:var(--green);box-shadow:0 0 7px var(--green)}.laser-cell.waiting{border-color:#18749a;background:#102d3a;color:#bae6fd}.laser-cell.waiting:before{background:var(--blue);box-shadow:0 0 7px var(--blue)}.laser-cell.normal{border-color:#a16207;background:#3b2b0d;color:#fde68a}.laser-cell.normal:before{background:var(--amber);box-shadow:0 0 7px var(--amber)}.laser-cell.emergency{border-color:#b91c1c;background:#451a1a;color:#fecaca}.laser-cell.emergency:before{background:var(--red);box-shadow:0 0 8px var(--red)}.laser-cell.group-selected{border-style:dashed;border-color:#38bdf8}.laser-cell.selected{outline:3px solid #e0f2fe;outline-offset:2px;z-index:1}.laser-cell:disabled{cursor:not-allowed;opacity:.62}.id-legend{display:flex;gap:14px;flex-wrap:wrap;margin-top:10px;color:var(--muted);font-size:11px}.id-legend span{display:flex;align-items:center;gap:6px}.legend-dot{width:8px;height:8px;border-radius:50%;background:#475569}.legend-dot.online{background:var(--green)}.legend-dot.normal{background:var(--amber)}.legend-dot.emergency{background:var(--red)}.selection-summary{margin-top:12px;padding:11px 13px;border:1px solid var(--border);border-radius:10px;background:#121c2e;color:#cbd5e1;font-size:12px;line-height:1.55}.selection-summary strong{color:#bae6fd}"
    ".confirm-modal{width:min(460px,calc(100% - 32px));padding:0;border:1px solid var(--border2);border-radius:16px;background:var(--surface);color:var(--text);box-shadow:0 28px 80px rgba(0,0,0,.58)}.confirm-modal::backdrop{background:rgba(2,6,23,.76);backdrop-filter:blur(3px)}.confirm-card{padding:22px}.confirm-head{display:flex;align-items:flex-start;gap:13px}.confirm-icon{display:grid;place-items:center;flex:0 0 42px;width:42px;height:42px;border-radius:11px;background:#3b2b0d;color:#fbbf24;border:1px solid #7c5d20}.confirm-icon svg{width:21px;height:21px}.confirm-title{margin:0;font-size:19px}.confirm-copy{margin:5px 0 0;color:var(--muted);font-size:13px;line-height:1.5}.confirm-data{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:18px}.confirm-item{padding:11px 12px;border:1px solid var(--border);border-radius:9px;background:#111b2d}.confirm-item span{display:block;color:var(--muted);font-size:10px;font-weight:800;letter-spacing:.06em;text-transform:uppercase}.confirm-item strong{display:block;margin-top:5px;font:700 14px 'Fira Code',monospace}.confirm-warning{margin-top:12px;padding:11px 12px;border-left:3px solid var(--amber);border-radius:7px;background:#302614;color:#fde68a;font-size:12px;line-height:1.5}.confirm-actions{display:grid;grid-template-columns:1fr 1.4fr;gap:10px;margin-top:18px}.confirm-cancel,.confirm-send{min-height:46px;border-radius:10px;font-weight:800;cursor:pointer}.confirm-cancel{border:1px solid var(--border2);background:#22304a;color:var(--text)}.confirm-cancel:hover{background:#2b3b57}.confirm-send{border:0;background:#0284c7;color:#fff}.confirm-send:hover{background:#0ea5e9}"
    ".form-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.field{display:flex;flex-direction:column;gap:7px}.field.full{grid-column:1/-1}.field label{color:#cbd5e1;font-size:12px;font-weight:700}.input,select{width:100%;min-height:44px;border:1px solid var(--border2);border-radius:9px;background:#111b2d;color:var(--text);padding:0 12px;font:inherit}.input:focus,select:focus{border-color:var(--blue)}.check{min-height:44px;display:flex;flex-direction:row;align-items:center;gap:10px;color:#cbd5e1;font-size:13px}.check input{width:18px;height:18px;accent-color:var(--green)}.notice{padding:12px;border:1px solid #7c5d20;background:#302614;border-radius:10px;color:#fde68a;font-size:12px;line-height:1.5}.primary{width:100%;min-height:46px;border:0;border-radius:10px;background:#0284c7;color:#fff;font-weight:800;cursor:pointer}.primary:hover{background:#0ea5e9}.primary:disabled{opacity:.5;cursor:not-allowed}.feedback{min-height:21px;margin-top:10px;font-size:12px;color:var(--muted)}.feedback.ok{color:#86efac}.feedback.bad{color:#fca5a5}"
    ".matrix-panel{margin-top:16px;padding:20px}.zone-badge{color:#bae6fd;background:#0c4a6e;border:1px solid #0e7490}.matrix-layout{display:grid;grid-template-columns:minmax(0,1fr) 290px;gap:24px;align-items:start}.matrix-scroll{overflow-x:auto;padding-bottom:4px}.matrix{display:grid;grid-template-columns:44px repeat(8,minmax(44px,1fr));grid-template-rows:repeat(9,44px);gap:5px;min-width:436px;max-width:620px}.axis-btn,.matrix-cell,.corner{display:grid;place-items:center;border-radius:8px;font-family:'Fira Code',ui-monospace,monospace;font-size:11px}.axis-btn{border:1px solid var(--border2);background:#19263b;color:#cbd5e1;cursor:pointer}.axis-btn:hover{background:#263750}.axis-btn.selected{border-color:#38bdf8;background:#0c4a6e;color:#e0f2fe}.matrix-cell{border:1px solid #2d3b51;background:#121c2e;color:#64748b}.matrix-cell.active{border-color:#3c7c59;background:repeating-linear-gradient(135deg,#123524,#123524 7px,#17452f 7px,#17452f 14px);color:#bbf7d0}.matrix-cell.active:after{content:'+';font-size:15px;font-weight:800}.corner{color:var(--muted);border:1px dashed var(--border)}.matrix-meta{display:flex;flex-direction:column;gap:12px}.obstacle-card{padding:14px;border:1px solid #3c7c59;border-radius:11px;background:#123524}.obstacle-card.normal{border-color:#a16207;background:#3b2b0d}.obstacle-card.emergency{border-color:#b91c1c;background:#451a1a}.obstacle-kicker{font-size:10px;font-weight:800;letter-spacing:.09em;color:var(--muted)}.obstacle-value{display:block;margin-top:6px;font-size:18px;color:#86efac}.obstacle-card.normal .obstacle-value{color:#fde68a}.obstacle-card.emergency .obstacle-value{color:#fca5a5}.obstacle-detail{display:block;margin-top:5px;color:#cbd5e1;font-size:11px;line-height:1.45}.mask-box{padding:13px;border:1px solid var(--border);border-radius:10px;background:#121c2e}.mask-line{display:flex;justify-content:space-between;gap:12px;margin-top:7px;font-size:13px}.mask-line:first-child{margin-top:0}.mask-bits{color:#7dd3fc;letter-spacing:.08em}.matrix-actions{display:grid;grid-template-columns:1fr 1fr;gap:8px}.matrix-actions .mini-btn{min-height:44px}.legend{display:flex;align-items:center;gap:9px;color:var(--muted);font-size:12px}.legend-swatch{width:18px;height:18px;border:1px solid #3c7c59;border-radius:5px;background:repeating-linear-gradient(135deg,#123524,#123524 4px,#17452f 4px,#17452f 8px)}.protocol-note{padding:12px;border:1px solid #155e75;background:#102d3a;border-radius:10px;color:#bae6fd;font-size:12px;line-height:1.5}"
    ".footer{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin-top:16px;padding:0 4px}.footer-state{display:flex;align-items:center;gap:8px}"
    "@media(max-width:900px){.manage{grid-template-columns:1fr}.matrix-layout{grid-template-columns:1fr}.matrix-meta{display:grid;grid-template-columns:1fr 1fr}.matrix-meta .notice{grid-column:1/-1}}@media(max-width:820px){.hero{grid-template-columns:1fr}.health{display:grid;grid-template-columns:1fr 1fr}.kpis{grid-template-columns:repeat(2,1fr)}.details{grid-template-columns:1fr}}"
    "@media(max-width:560px){.shell{padding:16px}.topbar{align-items:flex-start;gap:8px}.brand{min-width:0}.actions{flex:0 0 44px}.api{display:none}.btn{width:44px;padding:0}.btn span{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}.laser-panel{align-items:flex-start;flex-direction:column}.id-block{width:100%;text-align:left}.laser-id{font-size:46px}.health{display:block}.health-row{min-width:0}.health-row strong{font-size:12px;overflow-wrap:anywhere}.kpis{grid-template-columns:repeat(2,minmax(0,1fr))}.kpi{min-height:116px;padding:16px}.label{font-size:10px}.frame{grid-template-columns:1fr}.frame-cell{border-right:0;border-bottom:1px solid var(--border)}.frame-cell:last-child{border-bottom:0}}"
    "@media(max-width:560px){.matrix-meta{display:flex}.matrix-panel,.manage-card{padding:16px}.id-grid{gap:6px}}@media(max-width:460px){.form-grid{grid-template-columns:1fr}.field.full{grid-column:auto}}@media(max-width:390px){.kpis{grid-template-columns:1fr}.brand .kicker{display:none}.title{font-size:19px}}@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important;transition:none!important}}"
    "</style></head><body><a class='skip' href='#main'>Bỏ qua tới nội dung chẩn đoán</a><div class='shell'>"
    "<header class='topbar'><div class='brand'><div class='mark' aria-hidden='true'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='1.8'><path d='M4 17V7l8-4 8 4v10l-8 4-8-4Z'/><path d='m4 7 8 5 8-5M12 12v9'/></svg></div><div><div class='kicker'>CỔNG KẾT NỐI AUBOT</div><h1 class='title'>Giám sát Laser CAN</h1></div></div>"
    "<div class='actions'><a class='api' href='/api/debug/status' target='_blank' rel='noopener'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' aria-hidden='true'><path d='M8 9 4 12l4 3m8-6 4 3-4 3m-3-8-2 10'/></svg>Dữ liệu JSON</a><button id='toggle' class='btn' type='button' aria-label='Tạm dừng cập nhật' aria-pressed='false'><svg id='toggleIcon' viewBox='0 0 24 24' fill='currentColor' aria-hidden='true'><path d='M8 5h3v14H8zm5 0h3v14h-3z'/></svg><span id='toggleText'>Tạm dừng</span></button></div></header>"
    "<main id='main'><section class='hero'><article class='panel laser-panel' aria-labelledby='laserTitle'><div><div id='laserStatus' class='statusline off' role='status' aria-live='polite'><span class='dot'></span><span id='laserStatusText'>Đang tìm cảm biến</span></div><h2 id='laserTitle' class='laser-title'>Chưa nhận diện Laser</h2><div id='laserHint' class='muted'>Đang chờ khung CAN phản hồi từ module.</div></div><div class='id-block'><div class='id-label'>Mã Laser</div><div id='laserId' class='laser-id empty'>--</div></div></article>"
    "<aside class='panel health' aria-label='Trạng thái kết nối'><div class='health-row'><span class='health-name'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' aria-hidden='true'><path d='M6 9V3m12 6V3M4 9h16v5a8 8 0 0 1-16 0V9Zm8 8v4'/></svg>Mạng Ethernet</span><span id='ethChip' class='chip'>ĐANG KIỂM TRA</span></div><div class='health-row'><span class='health-name'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' aria-hidden='true'><path d='M5 7h14v10H5zM8 4v3m8-3v3M8 17v3m8-3v3M2 10h3m14 0h3M2 14h3m14 0h3'/></svg>Mạng CAN</span><span id='canChip' class='chip'>ĐANG KIỂM TRA</span></div><div class='health-row'><span class='health-name'>Địa chỉ thiết bị</span><strong id='ethIp' class='mono'>169.254.1.1</strong></div></aside></section>"
    "<section class='kpis' aria-label='Số liệu chính'><article class='kpi'><div class='kpi-top'><span class='label'>Khung nhận</span><span class='kpi-icon'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' aria-hidden='true'><path d='M12 3v14m-5-5 5 5 5-5M5 21h14'/></svg></span></div><div id='rxFrames' class='value'>0</div><div id='rxMeta' class='sub'>Chưa có dữ liệu Laser</div></article>"
    "<article class='kpi'><div class='kpi-top'><span class='label'>Khung gửi thành công</span><span class='kpi-icon'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' aria-hidden='true'><path d='m5 12 4 4L19 6'/></svg></span></div><div id='txOk' class='value'>0</div><div id='txFail' class='sub'>0 lần thất bại</div></article>"
    "<article class='kpi'><div class='kpi-top'><span class='label'>Lỗi mạng CAN</span><span class='kpi-icon'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' aria-hidden='true'><path d='M12 9v4m0 4h.01M10.3 4.4 2.8 18a2 2 0 0 0 1.8 3h14.8a2 2 0 0 0 1.8-3L13.7 4.4a2 2 0 0 0-3.4 0Z'/></svg></span></div><div id='busErrors' class='value'>0</div><div id='busMeta' class='sub'>CAN đang ổn định</div></article>"
    "<article class='kpi'><div class='kpi-top'><span class='label'>Phản hồi gần nhất</span><span class='kpi-icon'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' aria-hidden='true'><circle cx='12' cy='12' r='9'/><path d='M12 7v5l3 2'/></svg></span></div><div id='lastSeen' class='value'>--</div><div class='sub'>Thời gian từ khung cuối</div></article></section>"
    "<section class='details'><article class='panel detail'><div class='panel-head'><h2 class='panel-title'>Khung CAN gần nhất</h2><span id='lastType' class='type'>--</span></div><div class='frame'><div class='frame-cell'><div class='label'>Mã CAN</div><div id='lastId' class='frame-value'>--</div></div><div class='frame-cell'><div class='label'>Độ dài DLC</div><div id='lastDlc' class='frame-value'>--</div></div><div class='frame-cell'><div class='label'>Mã Laser</div><div id='frameLaserId' class='frame-value'>--</div></div></div><div class='trend' aria-label='Xu hướng số khung nhận'><svg viewBox='0 0 100 40' preserveAspectRatio='none' role='img'><title>Xu hướng khung CAN nhận được</title><path class='trend-base' d='M0 39H100'/><polyline id='trendLine' class='trend-line' points='0,39 100,39'/></svg></div></article>"
    "<article class='panel detail'><div class='panel-head'><h2 class='panel-title'>Chẩn đoán CAN</h2><span id='canStateText' class='type'>--</span></div><div id='errorSummary' class='error-summary' role='status' aria-live='polite'><strong id='errorKind' class='error-kind'>Đang kiểm tra</strong><span id='errorHelp' class='error-help'>Chưa có dữ liệu chẩn đoán.</span></div><div class='error-grid'><div class='error-item'><span>Lỗi bit</span><strong id='bitErrors' class='mono'>0</strong></div><div class='error-item'><span>Lỗi định dạng</span><strong id='formErrors' class='mono'>0</strong></div><div class='error-item'><span>Lỗi nhồi bit</span><strong id='stuffErrors' class='mono'>0</strong></div><div class='error-item'><span>Lỗi ACK</span><strong id='ackErrors' class='mono'>0</strong></div><div class='error-item'><span>Mất phân xử</span><strong id='arbLost' class='mono'>0</strong></div><div class='error-item'><span>Cờ lỗi gần nhất</span><strong id='lastErrorFlags' class='mono'>0x00</strong></div></div><div class='rows'><div class='row'><span class='key'>Bộ đếm lỗi TX / RX</span><strong id='errors' class='mono'>--</strong></div><div class='row'><span class='key'>Khung RX từ chối / bỏ</span><strong id='drops' class='mono'>--</strong></div><div class='row'><span class='key'>TX thất bại</span><strong id='txFailDetail' class='mono'>--</strong></div></div></article></section>"
    "<section class='panel matrix-panel' aria-labelledby='matrixTitle'><div class='panel-head'><div><h2 id='matrixTitle' class='panel-title'>Cấu hình vùng quan sát 8×8</h2><div class='muted'>Chọn hàng từ trên xuống dưới và cột từ phải sang trái</div></div><span class='type zone-badge'>CẤU HÌNH VÙNG</span></div><div class='matrix-layout'><div><div class='matrix-scroll'><div id='matrixGrid' class='matrix' role='grid' aria-label='Ma trận cấu hình Laser 8 hàng 8 cột'></div></div><div class='legend'><span class='legend-swatch'></span><span>Ô đang bật để phát hiện vật cản (bit 0)</span></div></div><aside class='matrix-meta'><div id='obstacleCard' class='obstacle-card'><span id='obstacleKicker' class='obstacle-kicker'>VẬT CẢN THEO NHÓM</span><strong id='obstacleValue' class='obstacle-value'>KHÔNG CÓ VẬT</strong><span id='obstacleDetail' class='obstacle-detail'>Chưa nhận event vật cản.</span></div><div class='mask-box'><div class='mask-line'><span>Mặt nạ hàng</span><strong id='rowMaskValue' class='mono'>0</strong></div><div class='mask-line'><span>Nhị phân</span><strong id='rowMaskBits' class='mono mask-bits'>00000000</strong></div><div class='mask-line'><span>Mặt nạ cột</span><strong id='colMaskValue' class='mono'>0</strong></div><div class='mask-line'><span>Nhị phân</span><strong id='colMaskBits' class='mono mask-bits'>00000000</strong></div></div><div class='matrix-actions'><button id='matrixAll' class='mini-btn' type='button'>Bật toàn bộ</button><button id='matrixClear' class='mini-btn' type='button'>Tắt toàn bộ</button></div><div class='protocol-note'><strong>Quy ước firmware:</strong> bit 0 bật vùng, bit 1 tắt vùng. Trạng thái vật cản chỉ xác định theo nhóm, không xác định được ô cụ thể.</div></aside></div></section>"
    "<section class='manage' aria-label='Quản lý cảm biến'><article class='panel manage-card'><div class='panel-head'><div><h2 class='panel-title'>Sơ đồ Laser ID 8×8</h2><div class='muted'>Ô sáng là Laser đang online · bấm ô để chọn Laser · bấm N1–N8 để chọn nhóm cấu hình thực tế</div></div><span id='nodeCount' class='count'>0 / 64 ONLINE</span></div><div class='id-grid-wrap'><div id='idGrid' class='id-grid' role='grid' aria-label='Ma trận trạng thái 64 Laser ID'></div></div><div class='id-legend' aria-label='Chú thích trạng thái'><span><i class='legend-dot'></i>Ngoại tuyến</span><span><i class='legend-dot online'></i>Không có hàng</span><span><i class='legend-dot normal'></i>Có hàng</span></div><div id='selectionSummary' class='selection-summary' role='status'>Chưa chọn Laser hoặc nhóm cấu hình.</div></article>"
    "<article class='panel manage-card'><div class='panel-head'><div><h2 class='panel-title'>Cấu hình Laser</h2><div class='muted'>Chuẩn B300 · cấu hình được áp dụng theo nhóm phần cứng</div></div><span id='formGroup' class='type'>--</span></div><form id='configForm'><div class='form-grid'><div class='field full'><label for='sensorSelect'>Laser tham chiếu</label><select id='sensorSelect' name='laser_id' disabled><option value=''>Chưa phát hiện cảm biến</option></select></div><div class='field'><label for='distance'>Khoảng cách cảnh báo (mm)</label><input id='distance' class='input' name='distance_mm' type='number' min='0' max='1200' value='600' required></div><div class='field'><label for='emergency'>Khoảng cách khẩn cấp (mm)</label><input id='emergency' class='input' name='emergency_mm' type='number' min='0' max='1200' value='300' required></div><div class='field'><label for='lowCol'>Mặt nạ cột thấp</label><input id='lowCol' class='input' name='low_col' type='number' min='0' max='255' value='0' required></div><div class='field'><label for='highRow'>Mặt nạ hàng cao</label><input id='highRow' class='input' name='high_row' type='number' min='0' max='255' value='15' required></div><label class='check field full'><input id='enabled' name='enabled' type='checkbox' checked> Bật phát hiện vật cản cho nhóm</label><div class='notice field full'>Do giao thức của firmware Laser, lệnh cấu hình luôn áp dụng cho toàn bộ nhóm phần cứng chứa Laser đang chọn. Khoảng cách khẩn cấp phải nhỏ hơn hoặc bằng khoảng cách cảnh báo.</div><button id='applyConfig' class='primary field full' type='submit' disabled>Gửi cấu hình qua CAN</button></div><div id='configFeedback' class='feedback' role='status' aria-live='polite'>Chọn một ô Laser hoặc nút nhóm ở ma trận bên trái.</div></form></article></section></main>"
    "<dialog id='configDialog' class='confirm-modal' aria-labelledby='confirmTitle'><div class='confirm-card'><div class='confirm-head'><span class='confirm-icon' aria-hidden='true'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M12 9v4m0 4h.01M10.3 4.4 2.8 18a2 2 0 0 0 1.8 3h14.8a2 2 0 0 0 1.8-3L13.7 4.4a2 2 0 0 0-3.4 0Z'/></svg></span><div><h2 id='confirmTitle' class='confirm-title'>Xác nhận cấu hình nhóm</h2><p id='confirmScope' class='confirm-copy'>Kiểm tra lại phạm vi và thông số trước khi gửi qua CAN.</p></div></div><div class='confirm-data'><div class='confirm-item'><span>Laser tham chiếu</span><strong id='confirmLaser'>--</strong></div><div class='confirm-item'><span>Nhóm tác động</span><strong id='confirmGroup'>--</strong></div><div class='confirm-item'><span>Cảnh báo</span><strong id='confirmDistance'>--</strong></div><div class='confirm-item'><span>Khẩn cấp</span><strong id='confirmEmergency'>--</strong></div></div><div id='confirmWarning' class='confirm-warning'>Các Laser trong nhóm sẽ khởi động lại để nhận cấu hình.</div><div class='confirm-actions'><button id='dialogCancel' class='confirm-cancel' type='button'>Quay lại</button><button id='dialogConfirm' class='confirm-send' type='button'>Xác nhận gửi</button></div></div></dialog>"
    "<footer class='footer'><span>Giám sát theo thời gian thực · ESP32-S3 + W5500</span><span class='footer-state'><span id='footerDot' class='dot'></span><span id='updated' class='mono'>Đang kết nối...</span></span></footer></div>"
    "<script>const $=id=>document.getElementById(id),set=(id,v)=>$(id).textContent=v;let paused=false,timer,history=[],selectedId=null,selectedGroup=null,selectionMode='sensor',pendingConfig=null,lastNodes=[],lastGroups=[],lastSlots=[];"
    "function cls(id,ok){$(id).className='chip '+(ok?'ok':'bad')}function drawTrend(v){history.push(Number(v)||0);if(history.length>24)history.shift();const min=Math.min(...history),max=Math.max(...history),span=Math.max(1,max-min),n=Math.max(1,history.length-1);$('trendLine').setAttribute('points',history.map((x,i)=>(i/n*100).toFixed(1)+','+(37-(x-min)/span*32).toFixed(1)).join(' '))}"
    "const configLabel=s=>({UNMANAGED:'Chưa quản lý',PENDING:'Đang chờ',SENT_UNVERIFIED:'Đã gửi',VERIFIED:'Đã xác nhận',MISMATCH:'Chưa khớp',FAILED:'Gửi lỗi'}[s]||s),canLabel=s=>({ACTIVE:'HOẠT ĐỘNG',WARNING:'CẢNH BÁO',PASSIVE:'THỤ ĐỘNG','BUS-OFF':'MẤT MẠNG',STOPPED:'ĐÃ DỪNG'}[s]||s);"
    "function renderCanErrors(c){const kinds=[{n:'Lỗi bit',v:c.bit_errors||0,h:'Kiểm tra baudrate, sample point, dây CAN và nhiễu điện.'},{n:'Lỗi định dạng',v:c.form_errors||0,h:'Khung CAN bị sai cấu trúc; thường liên quan chất lượng tín hiệu hoặc timing.'},{n:'Lỗi nhồi bit',v:c.stuff_errors||0,h:'Chuỗi bit trên bus không hợp lệ; kiểm tra termination, dây và nhiễu.'},{n:'Lỗi ACK',v:c.ack_errors||0,h:'Khung gửi không được node khác xác nhận; kiểm tra nguồn, đấu dây và baudrate.'},{n:'Mất phân xử',v:c.arbitration_lost||0,h:'Hai node cùng phát; CAN đã tự nhường cho ID ưu tiên hơn, không nhất thiết là lỗi vật lý.'}],top=kinds.reduce((a,b)=>b.v>a.v?b:a,kinds[0]),events=c.error_events||0,has=events>0||c.bus_error>0;set('bitErrors',c.bit_errors||0);set('formErrors',c.form_errors||0);set('stuffErrors',c.stuff_errors||0);set('ackErrors',c.ack_errors||0);set('arbLost',c.arbitration_lost||0);set('lastErrorFlags','0x'+Number(c.last_error_flags||0).toString(16).toUpperCase().padStart(2,'0'));const box=$('errorSummary');box.className='error-summary '+(c.state==='BUS-OFF'||c.state==='PASSIVE'?'bad-state':has?'warn-state':'');set('errorKind',has?(top.v?top.n:'Lỗi CAN khác'):'CAN ổn định');set('errorHelp',has?(top.v?top.h:'Driver đã ghi nhận lỗi; xem cờ lỗi gần nhất và bộ đếm TX/RX.'):'Chưa ghi nhận sự kiện lỗi kể từ khi ESP32 khởi động.')}"
    "function maskValue(id){const v=Math.max(0,Math.min(255,Number($(id).value)||0));$(id).value=v;return v}function maskBits(v){return v.toString(2).padStart(8,'0')}"
    "function renderMatrix(){const rows=maskValue('highRow'),cols=maskValue('lowCol'),order=[7,6,5,4,3,2,1,0];let html='<div class=\"corner\" aria-hidden=\"true\">H/C</div>';order.forEach(c=>{const enabled=!(cols&(1<<c));html+='<button class=\"axis-btn '+(enabled?'selected':'')+'\" type=\"button\" data-col=\"'+c+'\" aria-pressed=\"'+enabled+'\" aria-label=\"Bật hoặc tắt cột '+c+'\">C'+c+'</button>'});for(let r=0;r<8;r++){const rowEnabled=!(rows&(1<<r));html+='<button class=\"axis-btn '+(rowEnabled?'selected':'')+'\" type=\"button\" data-row=\"'+r+'\" aria-pressed=\"'+rowEnabled+'\" aria-label=\"Bật hoặc tắt hàng '+r+'\">H'+r+'</button>';order.forEach(c=>{const active=!!(rowEnabled&&!(cols&(1<<c)));html+='<div class=\"matrix-cell '+(active?'active':'')+'\" role=\"gridcell\" aria-selected=\"'+active+'\" aria-label=\"Hàng '+r+', cột '+c+(active?', đang bật':', đang tắt')+'\"></div>'})}$('matrixGrid').innerHTML=html;set('rowMaskValue',rows);set('rowMaskBits',maskBits(rows));set('colMaskValue',cols);set('colMaskBits',maskBits(cols));$('matrixGrid').querySelectorAll('[data-row]').forEach(b=>b.addEventListener('click',()=>{$('highRow').value=rows^(1<<Number(b.dataset.row));renderMatrix()}));$('matrixGrid').querySelectorAll('[data-col]').forEach(b=>b.addEventListener('click',()=>{$('lowCol').value=cols^(1<<Number(b.dataset.col));renderMatrix()}))}"
    "const groupForId=id=>id<=10?1:id<=20?2:id<=30?3:id<=40?4:id<=46?5:id<=52?6:id<=58?7:8,groupRange=g=>g<=4?((g-1)*10+1)+'–'+(g*10):(41+(g-5)*6)+'–'+(46+(g-5)*6),warehouseLabel=s=>({OFFLINE:'Ngoại tuyến',WAITING:'Đang chờ dữ liệu',EMPTY:'Không có hàng',OCCUPIED:'Có hàng',CRITICAL:'Có hàng'}[s]||'Chưa gán ô kho');"
    "function renderObstacle(){const node=lastNodes.find(n=>n.id===selectedId),group=node&&lastGroups.find(g=>g.group===node.group),state=node&&node.obstacle_valid?node.obstacle:(group?group.state:'CLEAR');$('obstacleCard').className='obstacle-card'+(state==='NORMAL'?' normal':state==='EMERGENCY'?' emergency':'');set('obstacleKicker','VẬT CẢN · '+(node?'LASER '+node.id+' · NHÓM '+node.group:'CHƯA CHỌN LASER'));set('obstacleValue',state==='EMERGENCY'?'CÓ VẬT · KHẨN CẤP':state==='NORMAL'?'CÓ VẬT · CẢNH BÁO':'KHÔNG CÓ VẬT');set('obstacleDetail',node&&node.obstacle_valid?'Trạng thái trực tiếp từ heartbeat của Laser.':!group||group.last_event_ago_ms<0?'Chưa nhận event vật cản.':'Event nhóm gần nhất '+group.last_event_ago_ms+' ms · Cảnh báo '+group.normal_events+' · Khẩn cấp '+group.emergency_events)}"
    "function renderIdGrid(){let html='';for(let row=0;row<8;row++){const group=row+1,groupOnline=lastNodes.filter(n=>n.group===group&&n.alive).length;html+='<button class=\"group-pick '+(selectionMode==='group'&&selectedGroup===group?'selected':'')+'\" type=\"button\" data-group=\"'+group+'\" aria-label=\"Chọn nhóm '+group+', Laser ID '+groupRange(group)+'\">N'+group+'<small>'+groupOnline+' online</small></button>';for(let col=0;col<8;col++){const id=row*8+col+1,n=lastNodes.find(x=>x.id===id),slot=lastSlots.find(x=>x.laser_id===id),alive=!!(n&&n.alive);let state=alive?'online':'';if(alive&&slot){state=slot.state==='OCCUPIED'||slot.state==='CRITICAL'?'normal':slot.state==='WAITING'?'waiting':'online'}else if(alive&&n.obstacle_valid)state=n.obstacle==='EMERGENCY'||n.obstacle==='NORMAL'?'normal':'online';const selected=id===selectedId?' selected':'',inGroup=selectedGroup===groupForId(id)?' group-selected':'',slotText=slot?slot.slot_code:'',detail=alive?(slot?warehouseLabel(slot.state):'Online, chưa gán ô kho'):'Ngoại tuyến';html+='<button class=\"laser-cell '+state+selected+inGroup+'\" type=\"button\" data-id=\"'+id+'\" '+(!n?'disabled':'')+' aria-label=\"Laser '+id+', '+detail+'\"><strong>'+id+'</strong><small>'+slotText+'</small></button>'}}$('idGrid').innerHTML=html;$('idGrid').querySelectorAll('[data-id]').forEach(b=>b.addEventListener('click',()=>selectSensor(b.dataset.id,true,'sensor')));$('idGrid').querySelectorAll('[data-group]').forEach(b=>b.addEventListener('click',()=>selectGroup(b.dataset.group)))}"
    "function updateSelection(){const n=lastNodes.find(x=>x.id===selectedId),slot=lastSlots.find(x=>x.laser_id===selectedId);if(!n){set('selectionSummary',selectedGroup?'Nhóm '+selectedGroup+' · ID '+groupRange(selectedGroup)+' · chưa phát hiện Laser online để làm tham chiếu.':'Chưa chọn Laser hoặc nhóm cấu hình.');return}$('selectionSummary').innerHTML='<strong>'+(selectionMode==='group'?'Nhóm '+n.group+' · ID '+groupRange(n.group):'Laser ID '+n.id)+'</strong> · '+(n.alive?'Online':'Mất phản hồi')+(slot?' · Ô kho '+slot.slot_code+' · '+warehouseLabel(slot.state):' · Chưa gán ô kho')+'<br>Cấu hình gửi xuống toàn bộ nhóm phần cứng '+n.group+'.'}"
    "function selectSensor(id,load,mode){selectedId=Number(id);const n=lastNodes.find(x=>x.id===selectedId);if(!n)return;selectionMode=mode||'sensor';selectedGroup=n.group;$('sensorSelect').value=String(selectedId);set('formGroup','NHÓM '+n.group);if(load&&n.status_valid){$('distance').value=n.distance_mm;$('emergency').value=n.emergency_mm;$('lowCol').value=n.low_col;$('highRow').value=n.high_row;$('enabled').checked=n.enabled}set('configFeedback',n.status_valid?'Đã nạp cấu hình Laser '+n.id+' đang báo.':'Laser chưa trả trạng thái DLC7; đang dùng giá trị nhập tay.');$('configFeedback').className='feedback';renderIdGrid();updateSelection();renderMatrix();renderObstacle()}"
    "function selectGroup(group){const g=Number(group),n=lastNodes.find(x=>x.group===g&&x.alive)||lastNodes.find(x=>x.group===g);selectedGroup=g;selectionMode='group';if(n)selectSensor(n.id,true,'group');else{selectedId=null;set('formGroup','NHÓM '+g);$('applyConfig').disabled=true;renderIdGrid();updateSelection()}}"
    "function renderNodes(nodes){lastNodes=nodes;const online=nodes.filter(n=>n.alive).length,occupied=lastSlots.filter(s=>s.state==='OCCUPIED'||s.state==='CRITICAL').length;set('nodeCount',online+' / 64 ONLINE · '+occupied+' CÓ HÀNG');const select=$('sensorSelect'),old=selectedId;select.innerHTML=nodes.length?nodes.map(n=>'<option value='+n.id+'>Laser '+n.id+' · Nhóm '+n.group+(n.alive?' · Online':' · Ngoại tuyến')+'</option>').join(''):'<option value=\"\">Chưa phát hiện cảm biến</option>';select.disabled=!nodes.length;$('applyConfig').disabled=!nodes.length;if(nodes.length&&(old===null||!nodes.some(n=>n.id===old)))selectSensor((nodes.find(n=>n.alive)||nodes[0]).id,true,'sensor');else{if(old!==null)select.value=String(old);renderIdGrid();updateSelection()}}"
    "function update(d){const eth=d.ethernet,can=d.can,l=d.laser,canOk=can.state==='ACTIVE';cls('ethChip',eth.connected);set('ethChip',eth.connected?'KẾT NỐI':'MẤT KẾT NỐI');cls('canChip',canOk);set('canChip',canLabel(can.state));set('ethIp',eth.ip||'169.254.1.1');"
    "$('laserStatus').className='statusline'+(l.detected?'':' off');set('laserStatusText',l.detected?l.node_count+' LaserID đang hoạt động':'Đang tìm cảm biến');set('laserTitle',l.detected?'Laser '+l.id+' đã kết nối':'Chưa nhận diện Laser');set('laserId',l.detected?l.id:'--');$('laserId').className='laser-id'+(l.detected?'':' empty');set('laserHint',l.detected?'Đang theo dõi theo thời gian thực '+l.node_count+' địa chỉ logic trên mạng CAN.':'Đang chờ khung CAN phản hồi từ module.');"
    "set('rxFrames',l.rx_frames);set('rxMeta',l.detected?'Khung Laser đã nhận':'Chưa có dữ liệu Laser');set('txOk',can.tx_ok);set('txFail',can.tx_fail+' lần thất bại');$('txFail').className='sub '+(can.tx_fail?'badtext':'good');set('busErrors',can.bus_error);set('busMeta',can.bus_error?'Đã ghi nhận lỗi trên bus':'CAN đang ổn định');$('busMeta').className='sub '+(can.bus_error?'warn':'good');set('lastSeen',l.detected?l.last_seen_ago_ms+' ms':'--');"
    "set('lastId',l.detected?'0x'+l.last_id.toString(16).toUpperCase().padStart(3,'0'):'--');set('lastDlc',l.detected?l.last_dlc:'--');set('frameLaserId',l.detected?l.id:'--');set('lastType',l.detected?(l.remote?'YÊU CẦU':'DỮ LIỆU'):'--');set('canStateText',canLabel(can.state));set('errors',can.tx_error+' / '+can.rx_error);set('drops',can.rx_rejected+' / '+can.rx_dropped);set('txFailDetail',can.tx_fail);renderCanErrors(can);set('updated','Cập nhật '+new Date().toLocaleTimeString('vi-VN'));$('footerDot').style.color=eth.connected?'var(--green)':'var(--red)';drawTrend(l.rx_frames);lastGroups=d.groups||[];renderNodes(d.nodes||[]);renderObstacle()}"
    "async function poll(){if(paused)return;try{const responses=await Promise.all([fetch('/api/debug/status',{cache:'no-store'}),fetch('/api/warehouse/status',{cache:'no-store'})]);if(!responses[0].ok||!responses[1].ok)throw Error('HTTP');const data=await Promise.all(responses.map(r=>r.json()));lastSlots=data[1].slots||[];update(data[0])}catch(e){set('updated','Không lấy được dữ liệu');$('footerDot').style.color='var(--red)'}timer=setTimeout(poll,500)}"
    "$('sensorSelect').addEventListener('change',e=>selectSensor(e.target.value,true));['lowCol','highRow'].forEach(id=>$(id).addEventListener('input',renderMatrix));$('matrixAll').addEventListener('click',()=>{$('lowCol').value=0;$('highRow').value=0;renderMatrix()});$('matrixClear').addEventListener('click',()=>{$('lowCol').value=255;$('highRow').value=255;renderMatrix()});"
    "$('configForm').addEventListener('submit',e=>{e.preventDefault();const distance=Number($('distance').value),emergency=Number($('emergency').value),n=lastNodes.find(x=>x.id===selectedId);if(!n)return;if(emergency>distance){set('configFeedback','Khoảng cách khẩn cấp phải ≤ khoảng cách cảnh báo.');$('configFeedback').className='feedback bad';return}pendingConfig={laser_id:selectedId,group:n.group,distance_mm:distance,emergency_mm:emergency,low_col:$('lowCol').value,high_row:$('highRow').value,enabled:$('enabled').checked?1:0};set('confirmLaser','ID '+selectedId);set('confirmGroup','Nhóm '+n.group+' · ID '+groupRange(n.group));set('confirmDistance',distance+' mm');set('confirmEmergency',emergency+' mm');set('confirmScope','Cấu hình từ Laser '+selectedId+' sẽ áp dụng cho nhóm phần cứng '+n.group+'.');set('confirmWarning','Các Laser ID '+groupRange(n.group)+' trong nhóm sẽ khởi động lại để nhận cấu hình.');$('configDialog').showModal()});"
    "$('dialogCancel').addEventListener('click',()=>{pendingConfig=null;$('configDialog').close()});$('configDialog').addEventListener('cancel',()=>{pendingConfig=null});$('dialogConfirm').addEventListener('click',async()=>{if(!pendingConfig)return;const request=pendingConfig;pendingConfig=null;$('configDialog').close();const body=new URLSearchParams(request);$('applyConfig').disabled=true;set('configFeedback','Đang gửi yêu cầu bắt tay cấu hình...');try{const r=await fetch('/api/laser/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok)throw Error(await r.text());const result=await r.json();set('configFeedback','Đã yêu cầu Nhóm '+result.group+'. Đang chờ cảm biến nhận cấu hình.');$('configFeedback').className='feedback ok'}catch(err){set('configFeedback','Gửi thất bại: '+err.message);$('configFeedback').className='feedback bad'}finally{$('applyConfig').disabled=false}});"
    "$('toggle').addEventListener('click',()=>{paused=!paused;$('toggle').setAttribute('aria-pressed',paused);$('toggle').setAttribute('aria-label',paused?'Tiếp tục cập nhật':'Tạm dừng cập nhật');set('toggleText',paused?'Tiếp tục':'Tạm dừng');$('toggleIcon').innerHTML=paused?\"<path d='m8 5 11 7-11 7z'/>\":\"<path d='M8 5h3v14H8zm5 0h3v14h-3z'/>\";if(paused){clearTimeout(timer);set('updated','Đã tạm dừng')}else poll()});renderMatrix();poll();</script></body></html>";

#endif

static const char *can_state_name(bsp_can_state_t state)
{
    switch (state) {
    case BSP_CAN_STATE_ACTIVE: return "ACTIVE";
    case BSP_CAN_STATE_WARNING: return "WARNING";
    case BSP_CAN_STATE_PASSIVE: return "PASSIVE";
    case BSP_CAN_STATE_BUS_OFF: return "BUS-OFF";
    default: return "STOPPED";
    }
}

static esp_err_t page_handler(httpd_req_t *req)
{
    if (!gateway_auth_require_page(req, GW_PERMISSION_CAN_DEBUG, NULL)) return ESP_OK;
    return gateway_web_send_html(req, GATEWAY_DEBUG_PAGE);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!gateway_auth_require_api(req, GW_PERMISSION_CAN_DEBUG, NULL)) return ESP_OK;
    bsp_eth_status_t eth = { 0 };
    bsp_can_status_t can = { 0 };
    laser_can_bringup_status_t laser = { 0 };
    bsp_eth_get_status(&eth);
    bsp_can_get_status(&can);
    laser_can_bringup_get_status(&laser);

    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    const int64_t seen_ago_ms = laser.laser_detected && now_ms >= laser.last_seen_ms
                                    ? now_ms - laser.last_seen_ms : 0;
    char json[1024];
    const int len = snprintf(json, sizeof(json),
        "{\"uptime_ms\":%" PRId64 ","
        "\"ethernet\":{\"connected\":%s,\"ip\":\"%s\",\"gateway\":\"%s\"},"
        "\"can\":{\"state\":\"%s\",\"tx_error\":%u,\"rx_error\":%u,"
        "\"bus_error\":%" PRIu32 ",\"tx_ok\":%" PRIu32 ",\"tx_fail\":%" PRIu32 ","
        "\"rx_rejected\":%" PRIu32 ",\"rx_dropped\":%" PRIu32 ","
        "\"error_events\":%" PRIu32 ",\"arbitration_lost\":%" PRIu32 ","
        "\"bit_errors\":%" PRIu32 ",\"form_errors\":%" PRIu32 ","
        "\"stuff_errors\":%" PRIu32 ",\"ack_errors\":%" PRIu32 ","
        "\"last_error_flags\":%" PRIu32 "},"
        "\"laser\":{\"detected\":%s,\"id\":%u,\"node_count\":%u,"
        "\"rx_frames\":%" PRIu32 ",\"last_id\":%u,\"last_dlc\":%u,"
        "\"remote\":%s,\"last_seen_ago_ms\":%" PRId64 "},\"nodes\":[",
        now_ms, eth.connected ? "true" : "false", eth.ip, eth.gateway,
        can_state_name(can.state), can.tx_error_count, can.rx_error_count,
        can.bus_error_count, can.tx_success_count, can.tx_failed_count,
        can.rx_rejected_count, can.rx_queue_overflow_count,
        can.error_callback_count, can.arbitration_lost_count,
        can.bit_error_count, can.form_error_count,
        can.stuff_error_count, can.ack_error_count, can.last_error_flags,
        laser.laser_detected ? "true" : "false", laser.laser_id, laser.node_count,
        laser.rx_frame_count, laser.last_rx_id, laser.last_rx_dlc,
        laser.last_rx_remote ? "true" : "false", seen_ago_ms);
    if (len < 0 || (size_t)len >= sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "status serialization failed");
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send_chunk(req, json, len);
    bool first = true;
    for (uint8_t id = 1U; err == ESP_OK && id <= LASER_CAN_MAX_NODES; ++id) {
        laser_can_node_status_t node = { 0 };
        if (!laser_can_bringup_get_node(id, &node)) {
            continue;
        }
        const int64_t node_seen_ago = now_ms >= node.last_seen_ms
                                          ? now_ms - node.last_seen_ms : 0;
        const int node_len = snprintf(json, sizeof(json),
            "%s{\"id\":%u,\"group\":%u,\"alive\":%s,\"status_valid\":%s,"
            "\"obstacle_valid\":%s,\"obstacle\":\"%s\","
            "\"rx_frames\":%" PRIu32 ",\"last_seen_ago_ms\":%" PRId64 ","
            "\"enabled\":%s,\"distance_mm\":%u,\"emergency_mm\":%u,"
            "\"low_col\":%u,\"high_row\":%u,\"managed\":%s,"
            "\"config_state\":\"%s\",\"config_tx_count\":%" PRIu32 "}",
            first ? "" : ",", node.laser_id, (unsigned)(node.group + 1U),
            node.alive ? "true" : "false", node.status_valid ? "true" : "false",
            node.obstacle_valid ? "true" : "false",
            laser_can_obstacle_state_name(node.obstacle_state),
            node.rx_frame_count, node_seen_ago,
            node.proximity_enabled ? "true" : "false", node.distance_mm,
            node.distance_emergency_mm, node.low_col, node.high_row,
            node.config_managed ? "true" : "false",
            laser_can_config_state_name(node.config_state), node.config_tx_count);
        if (node_len < 0 || (size_t)node_len >= sizeof(json)) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = httpd_resp_send_chunk(req, json, node_len);
        first = false;
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, "],\"groups\":[", 12U);
    }
    first = true;
    for (uint8_t group = 0U; err == ESP_OK && group < LASER_CAN_GROUP_COUNT; ++group) {
        laser_can_group_status_t group_status = { 0 };
        if (!laser_can_bringup_get_group(group, &group_status)) {
            continue;
        }
        const int64_t event_ago = group_status.last_event_ms > 0LL &&
                                  now_ms >= group_status.last_event_ms
                                      ? now_ms - group_status.last_event_ms : -1LL;
        const int group_len = snprintf(json, sizeof(json),
            "%s{\"group\":%u,\"state\":\"%s\",\"obstacle\":%s,"
            "\"last_event_ago_ms\":%" PRId64 ",\"normal_events\":%" PRIu32 ","
            "\"emergency_events\":%" PRIu32 "}",
            first ? "" : ",", (unsigned)(group + 1U),
            laser_can_obstacle_state_name(group_status.state),
            group_status.state != LASER_OBSTACLE_CLEAR ? "true" : "false",
            event_ago, group_status.normal_event_count,
            group_status.emergency_event_count);
        if (group_len < 0 || (size_t)group_len >= sizeof(json)) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = httpd_resp_send_chunk(req, json, group_len);
        first = false;
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, "]}", 2U);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0U);
    }
    return err;
}

static bool parse_u32_field(const char *body, const char *key, uint32_t max,
                            uint32_t *value)
{
    char text[16] = { 0 };
    if (httpd_query_key_value(body, key, text, sizeof(text)) != ESP_OK ||
        text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed > max) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static esp_err_t config_handler(httpd_req_t *req)
{
    if (!gateway_auth_require_api(req, GW_PERMISSION_LASER_CONFIG, NULL)) return ESP_OK;
    if (req->content_len == 0U || req->content_len >= 256U) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request body");
    }
    char body[256] = { 0 };
    size_t received = 0U;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "incomplete request body");
        }
        received += (size_t)n;
    }

    uint32_t laser_id = 0, distance = 0, emergency = 0;
    uint32_t low_col = 0, high_row = 0, enabled = 0;
    if (!parse_u32_field(body, "laser_id", LASER_CAN_MAX_NODES, &laser_id) ||
        !parse_u32_field(body, "distance_mm", 1200U, &distance) ||
        !parse_u32_field(body, "emergency_mm", 1200U, &emergency) ||
        !parse_u32_field(body, "low_col", 255U, &low_col) ||
        !parse_u32_field(body, "high_row", 255U, &high_row) ||
        !parse_u32_field(body, "enabled", 1U, &enabled) || emergency > distance) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid config: require emergency <= distance <= 1200");
    }

    const laser_can_config_request_t config = {
        .laser_id = (uint8_t)laser_id,
        .distance_mm = (uint16_t)distance,
        .distance_emergency_mm = (uint16_t)emergency,
        .low_col = (uint8_t)low_col,
        .high_row = (uint8_t)high_row,
        .proximity_enabled = enabled != 0U,
    };
    uint8_t group = 0U;
    const esp_err_t err = laser_can_bringup_configure(&config, &group);
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "LaserID is not detected");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   esp_err_to_name(err));
    }

    char response[160];
    const int len = snprintf(response, sizeof(response),
        "{\"accepted\":true,\"laser_id\":%u,\"group\":%u,"
        "\"state\":\"PENDING_HANDSHAKE\"}", (unsigned)laser_id,
        (unsigned)(group + 1U));
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

esp_err_t debug_http_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 48;
    /* Debug JSON can include all 64 physical LaserIDs plus group status. */
    config.stack_size = 12288;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        s_server = NULL;
        return err;
    }

    const httpd_uri_t debug = { .uri = "/debug", .method = HTTP_GET, .handler = page_handler };
    const httpd_uri_t tech = { .uri = "/app/tech", .method = HTTP_GET, .handler = page_handler };
    const httpd_uri_t status = {
        .uri = "/api/debug/status", .method = HTTP_GET, .handler = status_handler
    };
    const httpd_uri_t tech_status = {
        .uri = "/api/tech/status", .method = HTTP_GET, .handler = status_handler
    };
    const httpd_uri_t tech_can = {
        .uri = "/api/tech/can", .method = HTTP_GET, .handler = status_handler
    };
    const httpd_uri_t tech_laser = {
        .uri = "/api/tech/laser", .method = HTTP_GET, .handler = status_handler
    };
    const httpd_uri_t configure = {
        .uri = "/api/laser/config", .method = HTTP_POST, .handler = config_handler
    };
    if ((err = gateway_web_theme_register(s_server)) != ESP_OK ||
        (err = gateway_auth_http_register(s_server)) != ESP_OK ||
        (err = gateway_admin_http_register(s_server)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &debug)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &tech)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &status)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &tech_status)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &tech_can)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &tech_laser)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &configure)) != ESP_OK ||
        (err = warehouse_http_register(s_server)) != ESP_OK ||
        (err = gateway_config_http_register(s_server)) != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Warehouse dashboard ready: http://169.254.1.1/");
    ESP_LOGI(TAG, "Debug dashboard ready: http://169.254.1.1/debug");
    return ESP_OK;
}
