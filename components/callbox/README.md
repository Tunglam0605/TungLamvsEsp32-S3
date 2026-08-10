# CallBox product/runtime

CallBox là lớp sản phẩm: nó ánh xạ I/O board thành nghiệp vụ, quản lý Mission và kết nối WCS. include chứa public component contract; private_include là persistence contract nội bộ; src là implementation.

Đặc tả wire protocol cho IT/WCS: [WCS MQTT interface](../../docs/WCS_MQTT_INTERFACE.md).

## Bootstrap và ownership

callbox_app_run khởi tạo NVS/config → sequence service → queues → BSP → I/O/output → Mission → Wi-Fi/AP → SNTP → network status → Ethernet → MQTT → tasks. NVS, sequence và các queue lõi là fatal khi lỗi; lỗi phần cứng/uplink được log để các đường còn lại tiếp tục.

| Module | Files | Owns | Inputs | Outputs/dependencies |
|---|---|---|---|---|
| Bootstrap | callbox_app.c | Composition | Config/NVS | BSP, Platform, tasks |
| Mission manager | state_machine.c, status.c | Mission/transaction/Comm | button + WCS event | output snapshot, MQTT events |
| Protocol | protocol_types.c | JSON parse | MQTT payload | protocol_command_t |
| Input | io_handler.c, button_gate.c, callbox_io.c | debounce/gate/map | BSP DI | ButtonMsg |
| Output | output_renderer.c, led_control.c | physical render only | application snapshot | BSP DO/buzzer |
| MQTT adapter | mqtt_client.c | ESP-MQTT/TX/status | Config, snapshot | app events, broker |
| Wi-Fi policy | wifi_init.c, network_link.c | profile/AP policy | Config, Platform state | network link |
| Portal | config_portal.c | HTTP session/config | HTTP | NVS/config runtime apply |
| Persistence | nvs_storage.c, callbox_config_store.c | Config schema | NVS | Config_t |
| Sequence | sequence_service.c, sequence_store.c | monotonic global seq | Mission | NVS persistence |
| Time | time_sync.c | product SNTP policy | Config | Platform time |

## Mission Manager

Mission Manager là single writer của mission, pending transaction và Comm. State là idle, queued, assigned, locked, completed. WCS authoritative: local CALL không tự queued; matching accepted mới queued. Policy chặn chuyển lùi: assigned chỉ từ queued, locked chỉ từ queued/assigned, completed/overdue chỉ áp dụng cho mission active. Context sequence/AGV được xóa khi task kết thúc nên lệnh cũ không thể kích hoạt lại mission.

Task1/Task2 độc lập. Admission CALL/CANCEL yêu cầu Comm READY, network link, MQTT và không có pending conflict. Retry transaction dùng cùng seq mỗi 5 giây, tối đa hai retry, deadline 15 giây. Sequence được persist trước khi expose nên reboot có thể skip số nhưng không reuse.

Cancel chỉ queued/assigned; khi cả hai hợp lệ, chọn CallSequence lớn hơn. Locked không cancelable. Cancel ngắn xử lý khi release; giữ Cancel 5 giây toggle Rescue AP và không gửi cancel ngắn.

## Input, output và feedback

io_handler sample 10 ms; debounce 100 ms; guard 300 ms. button_gate là lớp admission trước Mission, vì vậy spam/giữ nút không tạo nhiều transaction.

Output Renderer chỉ nhận snapshot read-only, derive LED/tower/buzzer và không mutate Mission/Comm/pending. LED task pending blink chậm; queued/assigned/locked ON; idle OFF; reject FLASH_3. Tower ưu tiên: Comm chưa ready đỏ blink → error đỏ → overdue vàng blink → mission active vàng → ready idle xanh.

DO1 là business buzzer: call 100 ms, assigned 100 ms, config saved 120 ms, cancel ACK 150 ms, transaction failure 650 ms. GPIO46 là feedback network riêng.

## MQTT, uplink và portal

MQTT adapter dùng ESP-MQTT TCP/TLS, QoS 1, LWT/status retained. Callback MQTT chỉ biến payload thành app event, không mutate Mission. agv_id tối đa 31 ký tự được giữ trong context Mission để reconcile qua sync; nó không xuất hiện trong heartbeat/status hiện tại. Network link là Wi-Fi STA OR W5500 Ethernet; topic không đổi.

Wi-Fi policy giữ tối đa năm profile, chọn SSID nhìn thấy mạnh nhất, quản lý AP/Rescue AP và runtime DHCP/static mapping. Portal trên AP/STA đều cần login, giới hạn năm lần sai trong một phút theo IP và cho đổi mật khẩu theo policy tối thiểu 12 ký tự. Portal flow là parse → validate → persist NVS → update Config_t → selective Wi-Fi/MQTT/SNTP apply. MQTT fail closed nếu thiếu username/password, trừ build development bật anonymous rõ ràng.

Runtime network path sau H.2.1:

    Portal save → wifi_apply_config → configure_sta_ip
                → platform_wifi_apply_sta_network_config

Điều này áp dụng cho cả DHCP và static; raw ESP-NETIF DHCP lifecycle thuộc Platform.

## Tasks, queues và concurrency

| Task | Priority | Nhịp/chức năng |
|---|---:|---|
| io_handler | 5 | 10 ms input sample |
| state_machine | 10 | 20 ms, owner business state |
| mqtt_comm | 8 | 100 ms supervisor/heartbeat |
| mqtt_tx | 7 | publish socket worker |
| output_renderer | 7 | output snapshot |
| buzzer_task | 6 | business feedback |
| network_status | 6 | 200 ms AP/network feedback |
| wifi_select | 5 | scan/profile selection |

Queue app event=24, button=16, I/O state=1, renderer snapshot=1, business buzzer=10, MQTT TX=24. Producer gửi queue cho owner; mutex dùng cho resource/config snapshot, không thay thế ownership.

## Error behavior và debt

Lỗi transaction tạo feedback; retry timeout không tự giả định WCS state. MQTT reconnect đưa Comm về syncing cho tới sync hợp lệ. TLS chờ SNTP time valid.

P2/P3 còn: sequence init mutex cleanup, AP-start lifecycle HW validation, MQTT config snapshot concurrency, multi-field status snapshot consistency và rescue feedback mailbox đơn slot.
