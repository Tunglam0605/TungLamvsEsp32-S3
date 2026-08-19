# CallBox SEWS — WCS / IT MQTT Interface Specification

Tài liệu này là hợp đồng tích hợp cho đội IT, backend và WCS. Hành vi Mission/DHCP có baseline 88669b8a35de7968d55a215d3102af5b049cdf0a; portal authentication hiện tại có thêm 77fb093e03ed891eff27083affe2f5d6d9eb71c1. Commit tài liệu không làm thay đổi firmware binary.

## 1. Phạm vi và nguyên tắc

Đường dữ liệu vận hành chuẩn là:

    CallBox → MQTT Broker → WCS / IT Backend

CallBox có thể dùng Wi-Fi STA hoặc Ethernet W5500. Sự khác nhau của uplink hoàn toàn trong suốt với MQTT và WCS: topic, Client ID và payload không đổi.

Web portal của ESP32 là giao diện commissioning/cấu hình cục bộ, không phải WCS API. WCS không nên phụ thuộc HTTP portal để vận hành mission.

**WCS là nguồn trạng thái authoritative.** Một lần nhấn nút vật lý chỉ tạo yêu cầu CALL; nó không tự đưa Mission sang queued. WCS phải trả lệnh hợp lệ để CallBox cập nhật state.

| CallBox chịu trách nhiệm | WCS chịu trách nhiệm |
|---|---|
| Đọc nút vật lý, tạo seq bền vững, gửi call/cancel | Nhận event, lưu/deduplicate transaction |
| Retry cùng seq, heartbeat, sync khi reconnect | Quyết định accepted/rejected/assigned/locked/completed/overdue |
| Hiển thị trạng thái authoritative từ WCS | ACK cancel, duy trì state mission authoritative |
| Tạm khóa admission khi chưa sync | Trả snapshot hai task cho sync_request |

## 2. Nhận dạng và topic

ID là logical WCS identity, không phải MAC. Với ID 001:

| Hạng mục | Giá trị |
|---|---|
| MQTT Client ID | AUBOT-Callbox-001 |
| Event, CallBox → WCS | callbox/001/event |
| Command, WCS → CallBox | callbox/001/cmd |
| Status, CallBox → WCS | callbox/001/status |

SSID AP có thể là CALLBOX-001-A1B2C3 để phân biệt phần cứng. Nó không thay đổi MQTT ID 001.

## 3. Kết nối MQTT

| Thuộc tính | Contract hiện tại |
|---|---|
| QoS | 1 cho event, command và status |
| Heartbeat | 1 giây |
| MQTT keepalive | 30 giây |
| MQTT reconnect timeout | 5 giây |
| Network timeout | 10 giây |
| Status | retained |
| Event | không retained |
| LWT retained | {"online":false} |

WCS phải publish command không retained. Retained command có thể được phát lại sau reconnect và trở thành lệnh cũ/stale.

Transport TCP dùng URI mqtt://; TLS dùng mqtts://. TLS dùng ESP certificate bundle, vẫn kiểm tra hostname/CN và chỉ bắt đầu khi thời gian SNTP hợp lệ. Nếu broker dùng CA nội bộ/private CA không nằm trong bundle, đây là dependency triển khai trust-store bổ sung, chưa phải capability đã provision sẵn.

## 4. Event từ CallBox

### 4.1 CALL

    {"type":"call","task":1,"seq":1042,"ts":1751791860}

| Field | Ý nghĩa |
|---|---|
| type | call |
| task | 1 = Exchange Cart; 2 = Supply Empty Cart |
| seq | sequence bền vững, unsigned 32-bit |
| ts | Unix-style timestamp |

### 4.2 CANCEL

    {"type":"cancel","task":1,"seq":1043,"ts":1751791861}

CANCEL là một transaction mới, có seq riêng. Ví dụ CALL task 1 có seq 100, CANCEL sau đó có seq 105. WCS phải trả cancel_ack/rejected với ref_seq=105, không phải 100.

### 4.3 SYNC REQUEST

    {"type":"sync_request","seq":1044,"ts":1751791862,"fw":"1.2.0"}

sync_request thuộc phạm vi thiết bị nên **không có task**. Sau MQTT connect/reconnect, Comm chuyển offline → syncing, CallBox gửi sync_request và không nhận CALL/CANCEL cục bộ cho tới sync hợp lệ.

## 5. seq, ref_seq, retry và deduplication

seq là transaction do CallBox khởi tạo. ref_seq là tham chiếu của lệnh WCS trả về.

    CALL seq=100
    accepted/assigned/locked/completed ref_seq=100

    CANCEL seq=105
    cancel_ack hoặc rejected cancel ref_seq=105

Retry interval là 5000 ms, tối đa 2 retry và deadline 15000 ms. Mọi retry giữ nguyên seq và timestamp transaction. Ví dụ call seq 1042 được publish tại t=0, retry tại t=5 s và t=10 s; WCS không được tạo ba mission.

WCS phải idempotent theo tối thiểu tổ hợp callbox_id + event type + seq, và nên persist khóa này. Timestamp chỉ để truy vết, không phải khóa correlation.

Task 1 và Task 2 độc lập, có thể cùng active. Backend không được áp đặt giả định một mission active duy nhất trên một CallBox.

## 6. Lệnh WCS trên callbox/{id}/cmd

Parser hiện nhận: accepted, assigned, locked, completed, cancel_ack, rejected, overdue, sync và config. Production mission contract gồm tất cả trừ config. config chỉ parser-recognized, chưa là remote configuration contract được khuyến nghị.

Các số JSON phải là number, không phải string. Command không phải sync phải có task=1 hoặc task=2. agv_id tối đa 31 ký tự không tính ký tự kết thúc. reason/state vượt buffer hoặc payload fragmented/không hợp lệ có thể bị bỏ qua.

### accepted

    {"type":"accepted","task":1,"ref_seq":1042,"ts":1751791865}

ref_seq phải khớp CALL seq đang pending và task phải idle. Call pending được xóa và Mission thành queued. `accepted` lặp lại khi task đã queued là no-op.

### assigned

    {"type":"assigned","task":1,"ref_seq":1042,"agv_id":"AGV03","ts":1751791870}

ref_seq vẫn là CALL seq gốc; Mission chỉ chuyển từ queued sang assigned. Lệnh lặp khi đã assigned là no-op; lệnh đến khi idle/locked bị bỏ qua. agv_id là tùy chọn, tối đa 31 ký tự. WCS nên gửi khi có identity assignment để CallBox giữ trong local mission context và khôi phục qua sync snapshot. agv_id không được publish trong heartbeat/status hiện tại.

### locked

    {"type":"locked","task":1,"ref_seq":1042,"ts":1751791880}

Mission chỉ chuyển từ queued/assigned sang locked. Lệnh lặp khi đã locked là no-op; chuyển lùi bị bỏ qua. Locked không thể bị hủy từ CallBox.

### completed

    {"type":"completed","task":1,"ref_seq":1042,"ts":1751791950}

Lệnh chỉ hợp lệ khi mission đang queued/assigned/locked. Mission trở về idle, xóa pending/CallSequence/agv_id và cảnh báo overdue; lệnh cũ cùng ref_seq sau đó không còn tương quan được.

### cancel_ack

    {"type":"cancel_ack","task":1,"ref_seq":1043,"ts":1751791900}

Phải khớp Cancel pending, task đích và CANCEL seq. Khi hợp lệ, Mission thành idle và CallBox phát feedback cancel acknowledged.

### rejected

reason hợp lệ: none, locked, duplicate, no_task, wcs_busy.

CALL bị từ chối:

    {"type":"rejected","task":1,"ref_seq":1042,"reason":"duplicate","ts":1751791900}

CANCEL bị từ chối:

    {"type":"rejected","task":1,"ref_seq":1043,"reason":"locked","ts":1751791900}

CallBox xác định rejected thuộc CALL hay CANCEL bằng transaction pending có seq khớp. Rejected CALL làm xóa pending, Mission về idle và báo lỗi. Rejected CANCEL không tự đưa Mission về idle: WCS vẫn authoritative. Với cancel reason locked, duplicate hoặc no_task CallBox yêu cầu sync lại; wcs_busy chỉ báo lỗi local để có thể thử lại sau.

WCS cũng dùng `rejected` để kết thúc một transport order đã được accepted/assigned/locked. Khi `task` đang active và `ref_seq` khớp CALL seq gốc (không phải Cancel seq), CallBox coi đây là task failure authoritative: chỉ task đó về `idle`, phát feedback lỗi, ghi `reason`, `order_name`, `agv_id`, không tự retry CALL và không ảnh hưởng task còn lại. Ví dụ:

    {"type":"rejected","task":1,"ref_seq":1042,"reason":"TransportOrder ended in state WITHDRAWN/FAILED/UNROUTABLE","order_name":"CB-001-1042-CALL_EMPTY","agv_id":"AGV-01","ts":1751791900}

### overdue

    {"type":"overdue","task":1,"ref_seq":1042,"ts":1751791960}

ref_seq phải tương quan CALL seq và mission phải đang queued/assigned/locked. Lệnh chỉ bật warning; không tự completed mission.

### sync

    {
      "type":"sync",
      "ref_seq":2001,
      "ts":1751792000,
      "task1_state":"assigned",
      "task1_seq":1042,
      "task1_agv_id":"AGV03",
      "task2_state":"idle",
      "task2_seq":0,
      "task2_agv_id":""
    }

sync.ref_seq phải bằng sync_request seq đang pending. task1_state và task2_state bắt buộc, hợp lệ: idle, queued, assigned, locked, completed. Snapshot queued/assigned/locked bắt buộc có taskN_seq khác 0; nếu thiếu, toàn bộ sync bị bỏ qua. completed được chuẩn hóa thành terminal idle. task1_seq/task2_seq và agv_id được parser đọc nếu có; WCS nên luôn gửi để snapshot đầy đủ.

Matching sync overwrite/reconcile cả hai task, xóa local CALL/CANCEL stale và chuyển Comm sang ready. Sync không khớp bị bỏ qua.

### Retry và timeout của sync_request

CallBox tạo một persistent seq khi bắt đầu sync transaction. Khi WCS chưa phản hồi, CallBox giữ nguyên transaction và retry cùng seq với backoff 5 → 10 → 20 → 40 giây, sau đó tối đa 300 giây mỗi lần. Không tạo seq mới hoặc ghi NVS mới chỉ vì WCS im lặng. WCS phải deduplicate retry cùng seq và có thể trả snapshot authoritative cho bất kỳ retry nào.

    t≈0 s:  sync_request seq=2001
    t≈5 s:  retry sync_request seq=2001
    t≈15 s: retry sync_request seq=2001
    t≈35 s: retry sync_request seq=2001

## 7. Status, online và LWT

### Mã hiệu vận hành tháp đèn

Mã hiệu là feedback tại thiết bị, không làm thay đổi contract MQTT. Đỏ nháy nhanh 250 ms nghĩa là không còn Wi-Fi STA hoặc Ethernet có IP. Đỏ nháy chậm 500 ms nghĩa là uplink còn nhưng MQTT broker không hoạt động. Đỏ nháy kép `180 ms ON → 180 ms OFF → 180 ms ON → nghỉ 1 giây` nghĩa là MQTT đã subscribe nhưng đang chờ `sync` WCS. Đỏ sáng là reject/FAILED khi không có task nào khác active. Vàng sáng là task queued/assigned/locked; vàng nháy chậm là overdue; xanh sáng là ready/idle.

DO1 là còi relay: mất uplink 1×700 ms, mất MQTT 2×120 ms, ready 2×100 ms; alert mạng chỉ phát khi chuyển trạng thái và nhắc lại sau 60 giây. Reject/FAILED phát 1×650 ms. Khi một task bị reject nhưng task còn lại active, tháp giữ vàng và chỉ LED task lỗi nháy ba lần.

Status retained đi lên callbox/{id}/status:

    {
      "online":true,
      "comm":"ready",
      "task1":"assigned",
      "task2":"idle",
      "rssi":-55,
      "uptime":86214,
      "time_synced":true,
      "fw":"1.2.0",
      "ts":1751791920
    }

comm có offline, syncing hoặc ready. task có idle, queued, assigned, locked, completed.

Heartbeat/status hiện tại không có agv_id, mission_id hoặc CallSequence. Correlation dùng seq/ref_seq; identity/state đầy đủ được khôi phục qua sync snapshot.

Trong lúc người dùng vừa gửi `call` nhưng WCS chưa trả `accepted`/`rejected`, transaction nội bộ là pending và LED task nháy chậm, nhưng status vẫn giữ `taskN:"idle"`. Chỉ WCS mới có quyền chuyển status task sang `queued` bằng `accepted`.

online=true không đồng nghĩa buttons đã vận hành: online=true + comm=syncing nghĩa MQTT lên nhưng CallBox vẫn chờ WCS sync. Dashboard nên hiển thị riêng online và operational-ready.

LWT retained chỉ là:

    {"online":false}

Vì vậy consumer phải chấp nhận retained status offline tối giản, không đòi các trường heartbeat còn lại.

## 8. Cancel policy

Chỉ queued và assigned cancelable; locked tuyệt đối không. Nếu Task 1 và Task 2 đều cancelable, CallBox chọn task có CallSequence lớn hơn rồi gửi task đích rõ ràng trong cancel event.

## 9. Trình tự tham chiếu

### CALL lifecycle

    Operator -> CallBox: press Task 1
    CallBox -> WCS: call(seq=1042)
    WCS -> CallBox: accepted(ref_seq=1042)   [queued]
    WCS -> CallBox: assigned(ref_seq=1042)   [assigned]
    WCS -> CallBox: locked(ref_seq=1042)     [locked]
    WCS -> CallBox: completed(ref_seq=1042)  [idle]

### CANCEL lifecycle

    active mission: CALL seq=1042
    Operator -> CallBox: Cancel
    CallBox -> WCS: cancel(seq=1050, task=1)
    WCS -> CallBox: cancel_ack(ref_seq=1050) [idle]

Hoặc WCS có thể trả rejected(ref_seq=1050, reason="locked"); CallBox không ép task về idle.

### Reconnect lifecycle

    MQTT disconnect -> Comm OFFLINE
    MQTT reconnect  -> Comm SYNCING
    CallBox -> WCS: sync_request(seq=2001)
    WCS -> CallBox: sync(ref_seq=2001, task1 snapshot, task2 snapshot)
    CallBox -> Comm READY

## 10. Broker, network và security checklist cho IT

| IT cần cấp | Mục đích |
|---|---|
| MQTT hostname/IP, port, TCP hay TLS | Endpoint |
| Username/password và ACL | Xác thực/phân quyền |
| VLAN/subnet; DHCP hoặc static range | Commissioning mạng |
| IP, netmask, gateway, DNS nếu static | STA static |
| SNTP/NTP nội bộ và fallback policy | Timestamp/TLS |
| Firewall rule CallBox → broker | Kết nối MQTT |
| TLS certificate chain/CA policy | TLS compatibility |
| Broker HA/restart policy | Reconnect/sync behavior |

Khuyến nghị ACL cho CallBox 001:

| Client | Publish | Subscribe |
|---|---|---|
| AUBOT-Callbox-001 | callbox/001/event, callbox/001/status | callbox/001/cmd |
| WCS | callbox/{id}/cmd | callbox/+/event, callbox/+/status |

Đây là khuyến nghị deployment; firmware không tự enforce broker ACL.

Portal qua STA hoặc AP đều yêu cầu username `admin`. Mật khẩu mặc định riêng thiết bị là `Aubot-<MAC6>-9`; firmware migrate giá trị legacy `aubot`, giới hạn thử đăng nhập và lưu bền vững mật khẩu mới. MQTT mặc định bắt buộc username/password; anonymous chỉ có trong build development được bật rõ ràng. Đây không phải WCS credential/API.

## 10.1 Giới hạn MQTT command inbound

Firmware chỉ nhận đúng topic callbox/{id}/cmd. Command bị bỏ qua nếu topic dài từ 160 byte trở lên, payload dài từ 512 byte trở lên, hoặc MQTT giao payload theo fragment (current_data_offset khác 0). Firmware không có reassembly fragment command.

task, seq, ref_seq, ts và taskN_seq phải là JSON number, không gửi dưới dạng chuỗi. Parser dùng buffer type 20 byte, reason 16 byte, taskN_state 16 byte, agv_id/taskN_agv_id 32 byte (tối đa 31 ký tự payload). Chuỗi quá dài có thể làm command parse fail; với trường tùy chọn có thể bị bỏ trống.

## 11. Khuyến nghị backend và xử lý lỗi

WCS nên persist ít nhất: callbox_id, task, event_seq, event_type, mission_id, mission_state, agv_id, created_at, updated_at. seq cần được persist để duplicate/reconnect không tạo mission mới.

- Unknown CallBox: reject/log theo policy backend.
- Duplicate hoặc stale event: không tạo mission khác.
- Payload invalid: log/reject, không làm crash consumer.
- CallBox offline: giữ authoritative mission state.
- Reconnect: trả sync từ state đã persist.

## 12. Ma trận nghiệm thu tích hợp

Các mục dưới đây là kế hoạch nghiệm thu, chưa được đánh dấu PASS bởi tài liệu.

| ID | Scenario | CallBox action | WCS action | PASS criteria |
|---|---|---|---|---|
| IT-01 | Task1 call | Press Task1 | Nhận call | seq/task đúng |
| IT-02 | Task2 call | Press Task2 | Nhận call | Task2 độc lập |
| IT-03 | accepted | Pending call | accepted cùng ref_seq | queued |
| IT-04 | assigned | queued | assigned + agv_id | assigned |
| IT-05 | locked | assigned | locked | cancel local bị chặn |
| IT-06 | completed | active | completed | idle |
| IT-07 | coexist | Call cả hai | xử lý hai task | hai state cùng tồn tại |
| IT-08 | cancel queued | Press Cancel | cancel_ack | task idle |
| IT-09 | cancel assigned | Press Cancel | cancel_ack | task idle |
| IT-10 | locked cancel | Press Cancel | không có cancel | không publish cancel |
| IT-11 | newest target | hai task cancelable | nhận cancel | task có CallSequence lớn hơn |
| IT-12 | cancel correlation | cancel seq riêng | cancel_ack ref cancel seq | ACK được nhận |
| IT-13 | cancel rejected | cancel pending | rejected locked | không ép idle, sync |
| IT-14 | call rejected | call pending | rejected duplicate | pending clear/error/sync |
| IT-15 | duplicate call | retry cùng seq | deduplicate | một mission |
| IT-16 | duplicate cancel | retry cùng seq | deduplicate | một cancel effect |
| IT-17 | MQTT disconnect | ngắt broker | — | Comm offline |
| IT-18 | reconnect | broker lên | trả sync | sync_request phát |
| IT-19 | wrong sync | syncing | sync ref sai | ignored |
| IT-20 | valid sync | syncing | snapshot hai task | Comm ready |
| IT-21 | heartbeat | online | consume status | mỗi 1 s, retained |
| IT-22 | LWT | mất kết nối đột ngột | consume retained LWT | online=false tối giản |
| IT-23 | broker restart | restart broker | reconnect/sync | state reconciled |
| IT-24 | CallBox reboot active | reboot device | trả sync | state restored |
| IT-25 | Wi-Fi-only | Wi-Fi uplink | MQTT | topics không đổi |
| IT-26 | Ethernet-only | W5500 uplink | MQTT | topics không đổi |
| IT-27 | TLS | TLS config | broker TLS | certificate/time valid |
| IT-28 | sync timeout/new seq | Không trả sync seq=2001 | Nhận sync seq=2002, bỏ old response | late ref=2001 ignored; current ref=2002 accepted |
