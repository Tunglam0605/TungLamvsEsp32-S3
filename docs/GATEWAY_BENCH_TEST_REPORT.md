# Gateway Bench Test Report

## Device

- Branch: `gateway-esp32-s3`
- Commit đầu đợt test: `cc7acf67c79aeb4f389ae030ce7a72eed5be81f3`
- Commit firmware đã test: `d1ff6de` (`fix(status): avoid diagnostic task stack overflow`)
- Ngày test: 2026-08-12, múi giờ Asia/Bangkok (UTC+07:00)
- ESP-IDF: `v6.1-dev`
- Target: `esp32s3`
- Serial: `COM19`, USB VID `303A`, ESP32-S3 QFN56 rev 0.2, USB Serial/JTAG
- Flash: 16 MB; PSRAM nhúng 8 MB
- Binary: `aubot_gateway.bin`, 1.189.168 byte (`0x122530`)
- App partition: 2 MB; còn trống `0xDDAD0` byte (43%)

## Hardware

- Gateway board: ESP32-S3, MAC kết thúc bằng `fc:1c`.
- CAN: 250 kbit/s, standard 11-bit; không thực hiện short bus hoặc fault có nguy cơ hỏng transceiver.
- Laser quan sát được: **2 node**, LaserID 2 và LaserID 64. Điều này khác điều kiện mục tiêu “1 Laser duy nhất”.
- Ethernet debug: laptop `169.254.113.205/16` nối trực tiếp Gateway `169.254.1.1/16`.
- Wi-Fi STA: kết nối SSID `AGV1`, DHCP `192.168.1.138`; không ghi mật khẩu vào báo cáo.
- MQTT bench: `wcs.aubot.vn:1883`, kết nối TCP có xác thực; credential không được ghi vào báo cáo.
- Cấu hình MQTT thử nghiệm đã được gỡ sau test bằng cách khôi phục nguyên ảnh NVS. Trạng thái cuối: broker rỗng, MQTT disconnected.

## Results

| ID | Test | Expected | Actual | Result |
|----|------|----------|--------|--------|
| PRE-01 | Đúng nhánh | `gateway-esp32-s3` | Đúng nhánh; không sửa/merge branch khác | PASS |
| BUILD-01 | Production build | Build thành công | Ninja build thành công; app còn 43% | PASS |
| FLASH-01 | Flash và verify | Ghi đúng board, SHA hợp lệ | Flash full ban đầu PASS; sau fix ghi app tại `0x10000`, SHA PASS | PASS |
| BOOT-01 | NVS load | Không lỗi | NVS load và cấu hình hiện tại hoạt động | PASS |
| BOOT-02 | BSP init | DI/I2C/DO/Buzzer init | Boot log xác nhận init thành công | PASS |
| BOOT-03 | Wi-Fi AP | AP khởi động | `AUBOT-GW-01`, IP `192.168.65.204` | PASS |
| BOOT-04 | STA manager | Task khởi động | Khởi động và lấy IP `192.168.1.138` | PASS |
| BOOT-05 | W5500 | Ethernet init | Link và IP debug `169.254.1.1` | PASS |
| BOOT-06 | CAN | CAN 250 kbit/s | CAN ACTIVE | PASS |
| BOOT-07 | Laser protocol | Task khởi động | GROUP_8, heartbeat TX và request RX hoạt động | PASS |
| BOOT-08 | Warehouse Manager | Task khởi động | API trả profile GROUP_8 và 8 vị trí | PASS |
| BOOT-09 | MQTT task | Task khởi động | Kết nối được broker khi cấu hình bench | PASS |
| BOOT-10 | HTTP/WebUI | Server khởi động | Các route và API phục vụ request | PASS |
| BOOT-11 | Diagnostic Manager | Task khởi động ổn định | Khởi động; lỗi tràn stack đã được sửa và retest | PASS |
| AP-01 | AP boot ON | AP xuất hiện khi boot | Boot log xác nhận AP ON | PASS |
| AP-02 | AP auto-off 5 phút | 300.000 ms ± scheduling | AP stop tại uptime `301.318–301.328 s` | PASS |
| AP-03 | AP manual ON/OFF | Manual không bị timer cũ tắt | ON, vẫn ON sau 10 s; OFF theo lệnh | PASS |
| WEB-01 | `/`, `/debug`, `/cau-hinh` | HTTP hoạt động | Route trả 200 qua STA; debug/config trả 200 qua ETH | PASS |
| WEB-02 | Realtime polling/refresh | Không crash, không mất request | 250/250 request liên tiếp thành công | PASS |
| WEB-03 | Login/session | API config được bảo vệ | Chưa login trả 401; login tạo session và đọc status/config được | PASS |
| WEB-04 | UTF-8 tiếng Việt | Không lỗi encoding | `charset=utf-8`, nội dung tiếng Việt đúng khi đọc HTTP | PASS |
| WEB-05 | Responsive/visual | Kiểm tra trình duyệt thật | Trình duyệt tích hợp không khả dụng; chưa kiểm tra trực quan nhiều kích thước | NOT TESTED |
| ETHDBG-01 | Link | Link up | Ethernet 100 Mbps, Gateway báo connected | PASS |
| ETHDBG-02 | IP | `169.254.1.1/16` | Đúng IP; gateway `0.0.0.0` | PASS |
| ETHDBG-03 | Ping | Ping thành công | `Test-Connection` thành công | PASS |
| ETHDBG-04 | HTTP | WebUI/debug truy cập được | `/debug`, `/cau-hinh`, API trả 200 | PASS |
| ETHDBG-05 | Không là production path | ETH DEBUG không tự bật MQTT | API báo `debug=true`, `uplink=false`; MQTT chỉ kết nối khi Wi-Fi STA production đang hoạt động | PASS |
| WIFI-01 | Saved profile/DHCP | STA kết nối | Có 2 profile lưu; kết nối `AGV1`, IP `192.168.1.138`, RSSI khoảng -39…-40 dBm | PASS |
| WIFI-02 | Reconnect | Tự kết nối lại | Sau reset chủ động, STA lấy lại cùng IP và MQTT reconnect | PASS |
| WIFI-03 | Static IPv4 | Nếu có môi trường | Không thay đổi cấu hình IP hiện tại | NOT TESTED |
| ETHUP-01 | Ethernet uplink DHCP/static | Có router Ethernet | Ethernet đang nối trực tiếp laptop ở DEBUG mode; không có router uplink | NOT TESTED |
| NET-01 | Wi-Fi/ETH priority và failover | Các chuỗi A–E | Không ngắt Wi-Fi/router và không chuyển ETH UPLINK trong bench này | NOT TESTED |
| MQTT-01 | Broker/auth | Kết nối TCP | Gateway connected tới broker bench | PASS |
| MQTT-02 | Availability | QoS 1 retained, online true | Subscriber mới nhận retained `online:true` | PASS |
| MQTT-03 | LWT | Reset đột ngột phát retained offline | Nhận `online:false`, sau đó `online:true` khi reconnect | PASS |
| MQTT-04 | Status | JSON_WAREHOUSE_V1 retained | Schema/type/gateway_id/seq/boot_id/profile/summary/positions hợp lệ | PASS |
| MQTT-05 | Summary | total = empty + occupied + unknown | `0 = 0 + 0 + 0`; không có vị trí enabled | PASS |
| MQTT-06 | Event ping | `ping` tạo pong | Nhận event `type:"pong"`, QoS 1, non-retain | PASS |
| MQTT-07 | Request snapshot | Snapshot ngay | Nhận snapshot mới khoảng 177 ms sau publish command | PASS |
| MQTT-08 | Malformed commands | Ignore an toàn | `{}`, unknown, cmd số và payload >1 KB không crash/đổi Warehouse | PASS |
| MQTT-09 | Reconnect snapshot | Snapshot trong khoảng 100–200 ms + overhead | Có reconnect và snapshot, nhưng quan sát khoảng 1,04 s sau `online:true` qua broker từ xa | PARTIAL |
| CAN-01 | Passive heartbeat | TX ID `0x001`, DLC 0 | Gửi đều khoảng 2 giây | PASS |
| CAN-02 | Passive RX | Nhận frame Laser không cấu hình | Nhận remote request từ LaserID 2 và 64 | PASS |
| CAN-03 | CAN counters | Không drop/fail | ACTIVE; TX/RX tăng; tx_fail/rx_drop = 0 | PASS |
| CAN-04 | CAN error | Không lỗi kéo dài | Có 1 form error đơn lẻ ở một boot; không tăng thêm trong 140 s. Boot sau: 0 lỗi | PARTIAL |
| CAN-05 | Bus-off/recovery | Detect và recover | Không tạo bus-off có rủi ro phần cứng | NOT TESTED |
| LASER-01 | LaserID | Xác định từ raw frame | `0x079 REMOTE` → ID 2; `0x0B7 REMOTE` → ID 64 | PASS |
| LASER-02 | Status data frame | CAN ID = LaserID + 19, DLC/data hợp lệ | Chỉ thấy request config REMOTE DLC 0; chưa thấy status data | NOT TESTED |
| LASER-03 | Warn byte 7 | 0/1/2 mapping thực tế | Không có raw status frame để xác minh | NOT TESTED |
| LASER-04 | Sensor timeout | OFFLINE → Warehouse UNKNOWN | Không rút sensor/CAN | NOT TESTED |
| LASER-05 | Active configuration | Desired → verified | Cố ý không gửi config/reload/proximity/distance/mask | NOT TESTED |
| GROUP8-01 | Runtime profile | 8 Group và boundary đúng | UI/API trả đúng range; ID 2 → G1, ID 64 → G8 được quan sát; chưa test toàn bộ boundary bằng sensor | PARTIAL |
| GROUP12-01 | Profile 12 | 12 Group và boundary đúng | Bảng trong firmware đúng theo source; không có hardware môi trường 12 Group và không đổi profile | PARTIAL |
| WH-01 | Grid/mapping validation | Range/duplicate/distance/NVS | Grid GROUP_8 đọc được; không lưu mapping vì có thể kích active Laser config | NOT TESTED |
| WH-02 | Runtime initial state | UNKNOWN sau boot | 8/8 vị trí chưa cấu hình hiển thị UNKNOWN | PASS |
| BUZZ-01 | Transition patterns | Mỗi transition một pattern | Event AP/network/MQTT đã tạo; không có thiết bị đo âm thanh để xác minh pattern vật lý | PARTIAL |
| TOWER-01 | Tower logic | RED/YELLOW/GREEN theo network/MQTT | `gateway_indicator` hiện là stub trả `ESP_ERR_NOT_SUPPORTED`; state task chưa bật | FAIL |
| TOWER-02 | Tower physical DO | Không gán khi mapping TBD | Không gán, không kích DO | NOT TESTED |
| SOAK-01 | Quick-soak 30 phút | Không reset/leak/drop/reconnect loop | Dừng theo yêu cầu sau khoảng 12 phút 40 giây: không reset, CAN ACTIVE, TX/RX tăng, rx_drop=0; 4 lần HTTP timeout và form error tăng lên 5 | PARTIAL |

## CAN evidence

Các frame quan trọng thu trực tiếp từ serial:

```text
TX id=0x001 dlc=0
RX id=0x079 dlc=0 type=REMOTE data=[]
LASER DISCOVERED: LaserID=2, config request id=0x079 type=REMOTE
LaserID 2 requested config; group 0 is not managed yet
RX id=0x0B7 dlc=0 type=REMOTE data=[]
LASER DISCOVERED: LaserID=64, config request id=0x0B7 type=REMOTE
LaserID 64 requested config; group 7 is not managed yet
```

Snapshot API điển hình sau reset cuối:

```text
CAN state=ACTIVE, tx_error=0, rx_error=0, bus_error=0
tx_ok=38, tx_fail=0, rx_dropped=0
node_count=2, rx_frames=206
LaserID 2 alive=true status_valid=false config_state=UNMANAGED config_tx_count=0
LaserID 64 alive=true status_valid=false config_state=UNMANAGED config_tx_count=0
```

Một lỗi đường truyền đơn lẻ ở boot trước:

```text
CAN health: state=1 tx_err=0 rx_err=0 bus_err=1 tx_ok=99 rx_cb=534 rx_drop=0
CAN errors: callbacks=1 last=0x04 arb=0 bit=0 form=1 stuff=0 ack=0
```

`form=1` giữ nguyên trong ít nhất 140 giây tiếp theo; bus vẫn ACTIVE và RX/TX tiếp tục tăng.

## MQTT evidence

Topic thực tế với Gateway ID hiện tại:

```text
gateway/GW-01/availability
gateway/GW-01/status
gateway/GW-01/event
gateway/GW-01/cmd
```

Availability retained sau connect:

```json
{"online":true,"gateway_id":"GW-01","ts":1786522074}
```

LWT và reconnect:

```text
1786522169.893  gateway/GW-01/availability  {"online":false,"gateway_id":"GW-01"}
1786522169.930  gateway/GW-01/availability  {"online":true,"gateway_id":"GW-01","ts":1786522168}
1786522170.971  gateway/GW-01/status        boot_id=613F88BC seq=1
```

Status sample (không có vị trí Warehouse enabled):

```json
{"schema":"JSON_WAREHOUSE_V1","type":"snapshot","gateway_id":"GW-01","seq":52,"boot_id":"B935C692","profile":"8_group","summary":{"total":0,"empty":0,"occupied":0,"unknown":0},"positions":[]}
```

Command/event sample:

```text
cmd   {"cmd":"ping"}
event {"schema":"JSON_WAREHOUSE_V1","type":"pong","gateway_id":"GW-01",...}
cmd   {"cmd":"request_snapshot"}
status snapshot mới nhận sau khoảng 177 ms
```

## Fault evidence

- Lỗi boot ban đầu tái hiện 3 lần ngay sau STA got-IP, backtrace dừng ở `_UserExceptionVector`; stack có pattern FreeRTOS `0xA5`.
- Root cause: task `gw_diag` cấp 4096 byte stack nhưng đặt mảng 64 `laser_can_node_status_t` cùng `warehouse_snapshot_t` trên stack.
- Fix tối thiểu: duyệt từng LaserID bằng một struct node duy nhất; không tăng stack tùy tiện.
- Sau fix: qua điểm crash cũ, không còn panic/reset; STA, HTTP, CAN và MQTT cùng hoạt động.
- Reset trong test LWT là reset chủ động bằng esptool, không phải reset tự phát.
- Không short CAN_H/CAN_L, không cố tạo bus-off, không rút nguồn Laser, không gửi active Laser config.

## Stability Evidence

Quick-soak dùng `tools/gateway_http_soak.ps1`, poll `/api/debug/status` và `/api/warehouse/status` mỗi 10 giây qua Ethernet debug. Raw log được giữ local trong `tools/logs/` và không commit.

Lần chạy cuối được dừng theo yêu cầu sau khoảng 12 phút 40 giây, chưa đạt mốc 30 phút:

```text
76 mẫu thành công, 4 lần HTTP timeout
uptime 879701 → 1639968 ms, không có uptime regression/reset
CAN ACTIVE, tx_ok 802, rx_frames 4362
rx_drop 0
bus_error/form_error tăng tới 5
```

Vì chưa đủ 30 phút và CAN form error còn tăng, kết quả soak giữ ở `PARTIAL`, không nâng thành PASS.

## Bugs found and fixes

1. **Gateway reset/panic sau khi Wi-Fi STA lấy IP**
   - Kết luận: stack overflow/corruption trong `gw_diag`.
   - Fix commit: `d1ff6de fix(status): avoid diagnostic task stack overflow`.
   - Regression: build PASS; boot, STA, CAN, HTTP và MQTT không còn panic tại điểm cũ.
2. **MQTT reconnect snapshot chậm hơn target trong một phép đo**
   - Quan sát: khoảng 1,04 giây qua broker từ xa, target 100–200 ms + overhead.
   - Chưa sửa vì chưa tách được độ trễ firmware, broker và đường truyền; cần đo lại với broker LAN/timestamp phía firmware.
3. **Một CAN form error đơn lẻ**
   - Không lặp/tăng trong cửa sổ tiếp theo; không đủ bằng chứng để sửa firmware.
   - Cần tiếp tục theo dõi termination, dây, connector, ground và nhiễu nếu counter tăng trên bench dài.
4. **Tower logic chưa được triển khai hoạt động**
   - `gateway_indicator_start()` hiện luôn trả `ESP_ERR_NOT_SUPPORTED` kể cả khi mapping được xác nhận.
   - Không sửa vì mapping DO vẫn TBD và task hiện tại cấm tự gán DO.

## Open issues

- Chưa nhận được status data frame của Laser, nên chưa chứng minh CAN ID `LaserID + 19`, byte 7 Warn, Distance/Distance_E, hàng/cột hay trạng thái có hàng.
- Hai LaserID 2 và 64 cùng phản hồi, không đúng điều kiện khởi đầu “1 Laser duy nhất”.
- Chưa test sensor timeout → Warehouse UNKNOWN bằng fault vật lý.
- Chưa test ETH UPLINK, network failover hoặc broker qua Ethernet router.
- Chưa test Warehouse mapping lưu NVS vì việc tạo managed group có thể khiến Gateway trả cấu hình khi Laser request.
- Chưa test active Laser configuration theo chủ ý an toàn.
- Chưa test Tower vật lý; Tower logic hiện chưa hoạt động.
- Chưa đo heap/minimum heap/task watermark vì API firmware hiện không xuất các trường này.

## Final readiness

**NOT READY FOR WCS INTEGRATION**.

MQTT topic/schema/auth/LWT/cmd đã giao tiếp thật với broker và phần reset loop đã được sửa. Tuy nhiên WCS chưa thể dựa vào trạng thái kho vì chưa có Laser status/Warn thực tế, chưa có mapping Warehouse enabled, chưa test timeout/failover, và Tower logic chưa hoạt động. Active Laser config tiếp tục giữ `NOT TESTED` cho tới khi xác nhận sensor bench an toàn.

## Final checklist

```text
BUILD                         PASS
FLASH                         PASS
BOOT                          PASS
AP 5 MIN                      PASS
AP MANUAL                     PASS
WEBUI                         PASS (HTTP/realtime; visual responsive NOT TESTED)
WIFI STA                      PASS
ETH DEBUG                     PASS
ETH UPLINK                    NOT TESTED
NETWORK FAILOVER              NOT TESTED
MQTT AVAILABILITY             PASS
MQTT LWT                      PASS
MQTT STATUS JSON              PASS
MQTT EVENT                    PASS (pong; Warehouse transition NOT TESTED)
MQTT CMD                      PASS
MQTT RECONNECT SNAPSHOT       PARTIAL
CAN PASSIVE                   PASS
LASER STATUS                  NOT TESTED
WARN CURRENT TRUTH            NOT TESTED
LASER TIMEOUT → UNKNOWN       NOT TESTED
GROUP 8                       PARTIAL
GROUP 12                      PARTIAL
LASER ACTIVE CONFIG           NOT TESTED
WAREHOUSE NVS                 NOT TESTED
BUZZER                        PARTIAL
TOWER LOGIC                   FAIL
TOWER PHYSICAL DO             NOT TESTED
SOAK                          PARTIAL (12 phút 40 giây; form error tăng tới 5)
READY FOR WCS INTEGRATION     NO
```
