# CallBox product/runtime

include/ là public component interface, private_include/ là contract nội bộ persistence, src/ là implementation. Không phải mọi header là API ổn định ngoài component.

## Bootstrap

callbox_app_run khởi tạo: NVS → config → sequence service → app-event queue → business buzzer queue → BSP → LED/output → IO → Mission → AP identity/callback → Wi-Fi → time sync → network status → Ethernet → delay 5 s → MQTT → tasks. NVS, sequence, app-event queue và buzzer queue lỗi là fatal; BSP/Wi-Fi/network-status/Ethernet lỗi được log và tiếp tục.

## Module ownership

| Module | Files | Sở hữu |
|---|---|---|
| bootstrap | callbox_app.c | product composition |
| mission | state_machine.c, status.c | WCS states/transactions |
| protocol | protocol_types.c | JSON command codec |
| input | io_handler.c, button_gate.c, callbox_io.c | debounce/gate/mapping |
| output | output_renderer.c, led_control.c | LEDs/tower/business buzzer |
| MQTT | mqtt_client.c | ESP-MQTT/TX/status |
| network | wifi_init.c, network_link.c | profiles/AP/uplink |
| portal | config_portal.c | HTTP config/session |
| feedback | network_status_task.c | AP LED/GPIO46 patterns |
| persistence | nvs_storage.c, callbox_config_store.c | Config schema |
| sequence | sequence_service.c, sequence_store.c | monotonic seq |
| time | time_sync.c | product SNTP policy |

## Tasks và queues

Tasks: io_handler 2048/5 (10 ms sample; debounce 100 ms, guard 300 ms), state_machine 3072/10 (20 ms), mqtt_comm 4096/8 (100 ms), mqtt_tx 3072/7, output_renderer 3072/7, buzzer_task 2048/6, network_status 3072/6 (200 ms), wifi_select 4096/5.

Queues: app event=24, button=16, I/O state=1, renderer snapshot=1, business buzzer=10, MQTT TX=24. Luồng là producer task → single owner consumer; output renderer chỉ derive snapshot, không mutate mission.

## Mission / Cancel / output

State: idle, queued, assigned, locked, completed. Nhấn call chỉ tạo pending CALL; matching WCS accepted mới queued, assigned/locked/completed do WCS. Admission cần COMM_READY, uplink, MQTT, task idle và không transaction xung đột. Task1/Task2 độc lập.

Retry 5 s, tối đa 2; retry giữ sequence. Sequence lưu high-watermark trước khi expose, vì vậy reset có thể skip nhưng không reuse seq.

Cancel ngắn xử lý lúc release; chỉ queued/assigned, chọn CallSequence lớn nhất. Hold Cancel ≥5 s toggle Rescue AP và không gửi cancel thường.

Task LED: pending blink chậm; queued/assigned/locked ON; idle OFF; error FLASH_3. Cancel pending blink chậm, ack FLASH_2. Tower priority: comm chưa ready đỏ blink → error đỏ ON → overdue vàng blink → mission active vàng ON → ready idle xanh ON. DO1 beep: call 100 ms, assigned 100 ms, config saved 120 ms, cancel ack 150 ms, transaction failed 650 ms.

## MQTT/WCS

Topics QoS1: callbox/{id}/event, cmd, status. Commands: accepted, assigned, locked, completed, cancel_ack, rejected, overdue, sync. Reject reasons: none, locked, duplicate, no_task, wcs_busy. Event call/cancel có type, task, seq, ts; sync_request có type, seq, ts, fw. Status retained: online, comm (offline/syncing/ready), task1, task2, rssi, uptime, time_synced, fw, ts. Reconnect vào syncing; matching sync restore cả hai mission và mở button gate.

## Wi-Fi, AP, portal

wifi_init sở hữu max 5 profiles, strongest visible selection, scan lock, Rescue AP và runtime network config. Background scan cap 40; portal cap 32; active scan 100–300 ms, show_hidden=true. AP auto-stop cần STA stable 30 s, AP active, rescue off, zero AP clients và portal inactive.

AP identity là CALLBOX-<id>-<MAC6>; MAC6 là ba byte cuối MAC factory viết hoa, fallback CALLBOX-<id> khi đọc MAC lỗi. Password luôn CALLBOX-<id>, không phải final SSID. AP dùng 192.168.65.204/24, channel 1, tối đa 4 client. GPIO46 network feedback: STA connected 2×100 ms tại 2000 Hz; disconnected 650 ms tại 1600 Hz; Rescue enable 3×100 ms tại 2000 Hz; Rescue disable 2×450 ms tại 1600 Hz, gap 150 ms. Callback Rescue AP nối Wi-Fi policy với network_status, không có dependency trực tiếp Mission → network feedback.

Routes: GET /, POST /login, GET /logo.jpg, POST /save, GET /api/wifi-scan, /api/config, /api/io-status, /api/status, /api/wifi-profiles; POST /api/wifi-profiles/delete, /api/session/open, /api/session/ping, /api/session/finish. Save: parse → validate → persist NVS → update Config_t → selective Wi-Fi/MQTT/SNTP runtime apply. Cookie cb_auth 30 phút; active lease 30 s.

## Persistence

Namespace callbox. Config keys: wifi_ssid, wifi_pass, wifi_dhcp, wifi_ip, wifi_mask, wifi_gw, wifi_dns, mqtt_broker, mqtt_port, mqtt_tls, mqtt_user, mqtt_pass, callbox_id, web_pass, sntp_primary, sntp_fallback, wifi_count, wifiN_ssid, wifiN_pass. Sequence key seq_num. Không rename/type-change schema khi cần tương thích NVS cũ.

Xem root README, platform Wi-Fi và BSP.
