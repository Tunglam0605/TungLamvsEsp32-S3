# CALLBOX FLASH & OTA ARCHITECTURE SPECIFICATION

**Project:** CallBox SEWS – ESP32-S3 POE ETH 8DI/8DO  
**Target branch:** `callbox-esp32-s3`  
**Status:** Architecture baseline / implementation guide for agents  
**Purpose:** Tài liệu nguồn để mọi agent triển khai Flash, NVS và OTA bám theo cùng một kiến trúc, tránh viết OTA theo kiểu feature cục bộ hoặc gắn chặt vào WebUI/CallBox business logic.

---

## 1. Mục tiêu

Tài liệu này định nghĩa kiến trúc Flash và OTA dài hạn cho CallBox.

Mục tiêu không chỉ là “có OTA”, mà là xây một subsystem OTA:

- ổn định cho môi trường công nghiệp;
- hỗ trợ rollback A/B;
- có kênh recovery;
- tái sử dụng được cho các dự án ESP32/ESP32-S3 khác;
- tách biệt source firmware, storage backend, product policy và output;
- dễ bổ sung nguồn firmware mới mà không sửa OTA core;
- tận dụng tối đa các primitive chính thức của ESP-IDF;
- giữ nguyên triết lý phân tầng hiện có: BSP → Platform → Service → Product.

OTA phải được coi là **reusable service**, không phải một feature riêng nằm trong `config_portal.c` hay `state_machine.c`.

---

## 2. Trạng thái hệ thống hiện tại

Các giả định nền hiện tại của CallBox:

- MCU: ESP32-S3.
- Flash vật lý: 16 MB.
- Framework: ESP-IDF 6.1.x.
- Firmware hiện tại dùng custom partition table.
- Partition application hiện tại là `factory` 2 MB.
- Chưa có `otadata`, `ota_0`, `ota_1`.
- Production build đã có định hướng:
  - Secure Boot V2;
  - signed application;
  - Flash Encryption release mode;
  - NVS Encryption.
- WebUI hiện đã dùng chung một HTTP server và có thể truy cập từ:
  - Rescue AP;
  - Wi-Fi STA;
  - các uplink/IP phù hợp với kiến trúc hiện hành.
- Firmware đã có:
  - `platform_nvs`;
  - `platform_wifi`;
  - `health_monitor`;
  - `output_renderer`;
  - business `state_machine`;
  - WebUI/config portal;
  - MQTT/WCS lifecycle.

### 2.1 Partition table hiện tại

Baseline cũ:

```csv
# Name,   Type, SubType, Offset,   Size,   Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  2M,
```

Layout này **không đủ cho OTA A/B**.

---

## 3. Nguyên tắc kiến trúc bắt buộc

Các từ khóa sau được hiểu theo nghĩa:

- **MUST**: bắt buộc.
- **SHOULD**: nên làm, chỉ được bỏ nếu có lý do kỹ thuật rõ ràng.
- **MAY**: tùy chọn.

### 3.1 Separation of concerns

OTA MUST được chia theo các boundary sau:

```text
Product / CallBox
    |
    +-- OTA Policy
    +-- OTA Presentation / Output
    +-- OTA Trigger
    |
    v
Generic OTA Service
    |
    +-- Session / Lifecycle
    +-- Image Validation
    +-- Event / Progress
    |
    v
Platform OTA
    |
    v
ESP-IDF esp_ota_* APIs
```

Generic OTA Service MUST NOT biết:

- Task 1 / Task 2;
- WCS;
- MQTT topic;
- nút Cancel;
- tower light;
- WebUI;
- Wi-Fi AP;
- Wi-Fi STA;
- file đến từ server hay browser.

### 3.2 Một OTA core, nhiều source adapter

Mọi nguồn firmware MUST đi vào cùng một session API.

Target contract:

```c
ota_session_begin(metadata);
ota_session_write(data, len);
ota_session_finish();

ota_session_abort(reason);
ota_activate();
```

Nguồn firmware chỉ làm nhiệm vụ đưa byte stream vào OTA session.

Ví dụ:

```text
WebUI AP ------\
WebUI STA ------> ota_session_write() ---> OTA Core ---> A/B Flash
HTTPS Server ---/
SD Card --------/
UART/Gateway ---/
Future Source --/
```

Không được tạo các OTA engine riêng như:

```text
web_ota_engine
server_ota_engine
sd_ota_engine
```

### 3.3 Một output model, nhiều consumer

OTA core MUST chỉ phát state/event.

Ví dụ:

```text
OTA_IDLE
OTA_RECEIVING
OTA_VERIFYING
OTA_STAGED
OTA_INSTALLING
OTA_REBOOT_PENDING
OTA_PENDING_VERIFY
OTA_VALID
OTA_FAILED
OTA_ROLLBACK
```

Consumer có thể là:

- WebUI;
- Output Renderer;
- MQTT status;
- logging;
- diagnostics.

OTA core MUST NOT điều khiển trực tiếp GPIO, tower, relay hoặc buzzer.

### 3.4 Product policy là adapter

Generic OTA MUST NOT hardcode:

```c
if (task1 == IDLE && task2 == IDLE)
```

Thay vào đó phải có product policy kiểu:

```c
bool ota_policy_can_start(...);
```

CallBox implementation mới kiểm tra:

```text
Task1 == IDLE
AND Task2 == IDLE
AND no CALL pending
AND no CANCEL pending
AND OTA not busy
AND communication state phù hợp
```

Recovery mode có thể dùng policy riêng.

---

## 4. Flash Architecture 16 MB

### 4.1 Mục tiêu phân vùng

Flash được phân theo **lifecycle và write pattern**, không phân vụn theo từng module.

Nhóm chính:

```text
SYSTEM
CONFIGURATION
RUNTIME STATE
RECOVERY APPLICATION
APPLICATION A
APPLICATION B
DIAGNOSTICS
GENERAL STORAGE
FUTURE RESERVE
```

### 4.2 Layout mục tiêu sơ bộ

Đây là baseline kiến trúc. Offset cuối cùng MUST được xác minh bằng build, bootloader size, alignment và security settings trước khi freeze `partitions.csv`.

| Partition | Size mục tiêu | Owner | Vai trò |
|---|---:|---|---|
| bootloader | ESP-IDF | Bootloader | Boot + Secure Boot |
| partition table | ESP-IDF | Bootloader | Flash map |
| `nvs` | giữ legacy tối thiểu | Platform | Migration/system compatibility |
| `phy_init` | 4 KB | ESP-IDF | PHY init |
| `factory` | 2 MB | Recovery | Recovery firmware |
| `otadata` | 8 KB | Bootloader | A/B boot metadata |
| `nvs_cfg` | 128 KB | Platform NVS | Config lâu dài |
| `nvs_runtime` | 128 KB | Platform NVS | Runtime/high-write state |
| `coredump` | 256 KB | Diagnostics | Crash dump |
| `ota_0` | 4 MB | OTA | Application A |
| `ota_1` | 4 MB | OTA | Application B |
| `storage` | ~2 MB | Platform Storage | logs/assets/future |
| reserve | phần còn lại | Future | future expansion |

### 4.3 Vì sao OTA slot là 4 MB

Current app limit cũ là 2 MB.

Slot mới SHOULD là 4 MB để có headroom cho:

- OTA subsystem;
- HTTPS;
- WebUI tăng kích thước;
- diagnostics;
- certificate;
- protocol mới;
- feature tương lai.

Không được thiết kế slot chỉ đủ firmware hiện tại.

### 4.4 Factory/Recovery slot

`factory` SHOULD được giữ lại.

Mục tiêu dài hạn:

```text
factory = Minimal Recovery Firmware
```

Recovery firmware chỉ cần:

```text
BSP
Wi-Fi AP
WebUI
OTA
Basic diagnostics
```

Không bắt buộc có full business logic:

```text
Mission
WCS state machine
full MQTT workflow
Task1/Task2
```

Mục đích:

```text
Production A/B hỏng nặng
        |
        v
Recovery firmware
        |
        v
Rescue AP + WebUI
        |
        v
Upload firmware tốt
```

### 4.5 `otadata`

`otadata` là partition do ESP-IDF bootloader sở hữu.

MUST NOT dùng `otadata` để lưu:

- URL OTA;
- progress;
- OTA source;
- version manifest;
- business state;
- retry counter của application.

Application OTA metadata phải lưu ở `nvs_runtime`.

---

## 5. Persistent Storage Architecture

### 5.1 Không chia physical partition cho từng feature

Không tạo:

```text
wifi_nvs
mqtt_nvs
callbox_nvs
ota_nvs
```

trừ khi có lý do isolation/security đặc biệt.

Physical partition được chia theo lifecycle.

Logical ownership dùng namespace.

### 5.2 `nvs_cfg`

Dùng cho dữ liệu cấu hình ít thay đổi.

Ví dụ:

```text
nvs_cfg
|
+-- network
|   +-- Wi-Fi profiles
|   +-- DHCP/static
|   +-- IP/netmask/gateway/DNS
|   +-- SNTP
|
+-- mqtt
|   +-- broker
|   +-- port
|   +-- transport
|   +-- credentials
|
+-- callbox
|   +-- CallBox ID
|   +-- product configuration
|
+-- security
    +-- WebUI config
```

### 5.3 `nvs_runtime`

Dùng cho dữ liệu runtime hoặc ghi thường xuyên hơn.

Ví dụ:

```text
nvs_runtime
|
+-- sequence
|   +-- MQTT sequence high-watermark
|
+-- ota
|   +-- source type
|   +-- target version
|   +-- expected size
|   +-- written bytes
|   +-- job id/hash
|   +-- last error
|   +-- resume state
|
+-- health
    +-- runtime counters
    +-- diagnostics metadata
```

### 5.4 Mở rộng `platform_nvs`

Hiện platform abstraction là hướng đúng và MUST được giữ.

Cần mở rộng để support named partition.

Target API có thể tương đương:

```c
platform_nvs_init_partition(...);
platform_nvs_open_partition(...);
platform_nvs_commit(...);
platform_nvs_close(...);
```

Product layer MUST NOT gọi trực tiếp:

```c
nvs_open_from_partition()
```

---

## 6. Platform OTA Layer

Tạo component:

```text
components/platform/platform_ota/
```

`platform_ota` là nơi duy nhất được gọi trực tiếp các API ESP-IDF OTA.

Ví dụ:

```c
esp_ota_get_next_update_partition();
esp_ota_begin();
esp_ota_write();
esp_ota_end();
esp_ota_abort();
esp_ota_set_boot_partition();

esp_ota_get_running_partition();
esp_ota_get_state_partition();

esp_ota_mark_app_valid_cancel_rollback();
esp_ota_mark_app_invalid_rollback_and_reboot();
```

### 6.1 Trách nhiệm `platform_ota`

`platform_ota` chịu trách nhiệm provider mechanics:

- chọn inactive slot;
- begin write;
- streaming write;
- finalize;
- abort;
- set boot partition;
- đọc running/next partition;
- map lỗi ESP-IDF sang contract của service;
- rollback primitives.

`platform_ota` MUST NOT biết:

- WCS;
- CallBox;
- WebUI;
- server URL;
- MQTT;
- button;
- tower.

---

## 7. Generic OTA Service

Đề xuất component:

```text
components/services/ota/
```

### 7.1 Các phần chính

```text
ota_types
ota_service
ota_session
ota_validator
ota_events
```

### 7.2 State model

Minimum state model:

```text
IDLE
 |
 v
ADMISSION
 |
 v
RECEIVING
 |
 v
VERIFYING
 |
 +------ invalid ------> FAILED
 |
 v
STAGED
 |
 v
INSTALLING
 |
 v
REBOOT_PENDING
 |
 v
PENDING_VERIFY
 |
 +------ health fail ---> ROLLBACK
 |
 v
VALID
```

### 7.3 Image metadata

Generic metadata SHOULD chứa tối thiểu:

```c
typedef struct {
    char project_name[...];
    char version[...];
    uint32_t size;
    uint32_t secure_version;
    uint8_t source_type;
} ota_image_info_t;
```

Không hardcode CallBox fields vào generic metadata.

### 7.4 Streaming

OTA MUST stream.

MUST NOT:

```c
malloc(full_firmware_size);
```

MUST dùng bounded buffer, ví dụ 4–8 KB.

Data path:

```text
source
  |
  v
small buffer
  |
  v
ota_session_write()
  |
  v
platform_ota_write()
  |
  v
inactive slot
```

---

## 8. OTA Source Adapters

### 8.1 Phase hiện tại: WebUI Upload

WebUI AP và WebUI STA là **cùng một source adapter**.

Khác nhau chỉ ở network path.

```text
Browser via AP ----\
                    > ota_web_source ---> OTA Service
Browser via STA ---/
```

HTTP API dự kiến:

```text
GET  /api/ota/status
POST /api/ota/upload
POST /api/ota/install
POST /api/ota/discard
```

Upload SHOULD dùng:

```text
Content-Type: application/octet-stream
```

Không bắt buộc multipart nếu không có lý do UI.

### 8.2 WebUI upload flow

```text
Select .bin
   |
   v
POST /api/ota/upload
   |
   v
stream to inactive slot
   |
   v
validate
   |
   v
STAGED
   |
   v
User clicks "Install & Reboot"
   |
   v
set boot partition
   |
   v
restart
```

Upload thành công **không đồng nghĩa kích hoạt ngay**.

### 8.3 Future HTTPS Server source

Sau này:

```text
Firmware server
      |
      v
ota_https_source
      |
      v
same OTA Service
```

HTTPS source MAY có:

- manifest;
- version check;
- resumable download;
- HTTP Range;
- TLS server verification.

### 8.4 Future sources

Kiến trúc phải cho phép thêm:

```text
SD card
USB
UART gateway
CAN gateway
BLE
MQTT chunk
local storage
```

mà không sửa generic OTA core.

---

## 9. CallBox OTA Product Adapter

Đề xuất:

```text
components/callbox/
|
+-- ota_policy.c
+-- ota_web_handler.c
+-- ota_output_adapter.c
+-- ota_boot_validator.c
```

### 9.1 `ota_policy`

Normal mode baseline:

```text
Task1 == IDLE
Task2 == IDLE
no call transaction pending
no cancel transaction pending
OTA not busy
communication/WCS state safe
```

Normal mode SHOULD yêu cầu `COMM_READY`.

### 9.2 Recovery mode policy

Recovery mode có thể cho phép local OTA mà không yêu cầu WCS nếu:

- business runtime không chạy;
- local WebUI đã authenticated;
- OTA target hợp lệ.

Đây là recovery channel, không phải normal business OTA.

### 9.3 Trigger không thuộc OTA core

Trigger có thể là:

```text
WebUI upload
Cancel hold 10 s
remote command
service tool
future API
```

Trigger chỉ phát yêu cầu.

Không trigger nào được tự gọi trực tiếp `esp_ota_*`.

---

## 10. Output Architecture

OTA core phát event/state.

CallBox output adapter chuyển state thành UI/tower/buzzer.

Baseline tower:

```text
OTA_RECEIVING
OTA_VERIFYING
OTA_INSTALLING
    |
    v
RED -> YELLOW -> GREEN -> repeat
```

Ví dụ 250 ms mỗi màu.

Priority đề xuất:

```text
OTA_ACTIVE
>
network diagnostic
>
mission indication
```

Khi `OTA_STAGED` nhưng chưa install, tower SHOULD trở về normal indication.

OTA code MUST NOT sở hữu GPIO.

---

## 11. Cancel Button OTA Trigger – Future Phase

Không triển khai ở Phase WebUI đầu tiên, nhưng architecture phải để sẵn.

Target gesture:

```text
release < 5 s
    -> normal CANCEL

hold >= 5 s and release < 10 s
    -> toggle Rescue AP

6 s
    -> warning beep

7 s
    -> warning beep

8 s
    -> warning beep

9 s
    -> warning beep

10 s
    -> warning beep + OTA request
```

Tại 10 s:

```text
OTA_REQUEST
```

không có nghĩa flash ngay.

Phải qua `ota_policy`.

Nếu OTA request được dùng cho server OTA:

```text
Cancel 10 s
    |
    v
OTA trigger
    |
    v
ota_https_source
    |
    v
same OTA Service
```

---

## 12. Rollback Strategy

Rollback MUST là baseline production requirement.

Flow:

```text
Running A
   |
   v
write B
   |
   v
set B boot
   |
   v
reboot
   |
   v
B = PENDING_VERIFY
   |
   +---- validation fail ---> rollback A
   |
   v
mark B valid
```

### 12.1 Download success != OTA success

OTA chỉ được coi thành công khi:

```text
image write OK
AND image validation OK
AND boot new firmware OK
AND critical runtime health OK
```

CallBox có thể bổ sung:

```text
network init OK
MQTT operational
WCS sync/COMM_READY
critical tasks healthy
```

trước khi mark application valid.

### 12.2 Health Monitor reuse

Không viết một health subsystem mới chỉ cho OTA.

`ota_boot_validator` SHOULD reuse:

- `health_monitor`;
- critical task readiness;
- network readiness;
- MQTT/WCS readiness nếu policy yêu cầu.

---

## 13. Secure OTA

Production OTA MUST tương thích với security baseline hiện có.

Expected production chain:

```text
CI build
   |
   v
signed application
   |
   v
WebUI / HTTPS source
   |
   v
OTA inactive slot
   |
   v
Secure Boot verification
   |
   v
Flash Encryption
```

Không được làm một OTA path “debug-only” rồi vô tình dùng trong production.

Anti-rollback bằng eFuse là phase sau.

Phase đầu SHOULD tập trung:

```text
A/B
rollback
Secure Boot
Flash Encryption
image validation
```

Không bật anti-rollback eFuse policy trước khi release/versioning process đã được freeze và soak test.

---

## 14. WebUI OTA Safety

### 14.1 Authentication

OTA route MUST dùng cùng authentication policy hiện có hoặc mạnh hơn.

Không tạo route upload unauthenticated.

### 14.2 AP lifecycle

Trong OTA active:

```text
AP MUST NOT auto-stop
```

Nếu đang upload qua Rescue AP, `network_status_task` hoặc AP lifecycle MUST được informed bởi OTA state.

### 14.3 HTTP worker

Current portal dùng ESP-IDF HTTP server với bounded worker.

OTA upload handler MUST:

- stream request;
- không dùng helper nhận toàn body vào RAM;
- xử lý receive timeout riêng;
- không block vĩnh viễn;
- abort session sạch khi browser disconnect.

WebUI SHOULD tạm dừng polling status/io API trong lúc upload lớn nếu cần tránh contention.

---

## 15. Validation Rules

Trước khi cho `STAGED`, tối thiểu kiểm tra:

```text
valid ESP application image
target chip compatible
image fits target partition
project identity phù hợp
version metadata đọc được
Secure Boot signature policy phù hợp
```

SHOULD kiểm tra thêm:

```text
new version != current version
known-invalid version không được retry vô hạn
secure_version phù hợp
```

Không chỉ tin filename `.bin`.

---

## 16. Failure Handling

### 16.1 Browser disconnect

```text
upload interrupted
    |
    v
ota_session_abort()
    |
    v
inactive image invalid/not activated
    |
    v
current firmware continues
```

### 16.2 Validation fail

```text
esp_ota_end / validator fail
    |
    v
FAILED
```

Không reboot.

### 16.3 Power loss during inactive write

Current running slot MUST vẫn boot được.

### 16.4 New firmware boot fail

Bootloader rollback MUST đưa hệ thống về previous valid slot.

### 16.5 Runtime health fail

`ota_boot_validator` MUST mark new image invalid và rollback.

---

## 17. Migration Strategy

Đây là phần critical.

Thiết bị hiện tại đang dùng factory-only layout.

Không được coi việc chuyển từ:

```text
factory-only
```

sang:

```text
factory + otadata + ota_0 + ota_1 + new NVS partitions
```

là một OTA update bình thường.

### 17.1 First migration

First migration SHOULD là maintenance/provisioning có kiểm soát:

```text
flash bootloader
flash partition table
flash A/B-capable application
migrate persistent config
validate boot/security
```

Không tự update partition table từ WebUI OTA trong phase đầu.

### 17.2 Data migration

Agent MUST lập bảng:

```text
old namespace/key
    ->
new partition / namespace / key
```

Migration MUST:

- giữ credentials;
- giữ CallBox ID;
- giữ network config;
- giữ MQTT config;
- không làm sequence reuse;
- có schema version;
- không silently erase config khi lỗi migration.

---

## 18. Coredump & Diagnostics

Với flash 16 MB, SHOULD dành partition coredump.

Mục tiêu:

```text
panic / abort / watchdog
      |
      v
coredump partition
      |
      v
field diagnostics
```

OTA state SHOULD log:

```text
source
version
start time
bytes received
failure stage
error code
previous running version
target partition
```

Không log credential/token.

---

## 19. Storage Reserve

Nên reserve `storage` khoảng 2 MB và không ép sử dụng ngay.

Future use có thể gồm:

```text
diagnostic history
WebUI assets
certificate bundles
cached OTA manifest
maintenance export
logs
```

Tránh phải sửa partition table thường xuyên sau khi thiết bị đã triển khai.

---

## 20. Suggested Repository Structure

```text
components/
|
+-- bsp/
|
+-- drivers/
|
+-- platform/
|   +-- platform_nvs/
|   +-- platform_wifi/
|   +-- platform_time/
|   +-- platform_ota/
|
+-- services/
|   +-- ota/
|       +-- include/
|       |   +-- ota_service.h
|       |   +-- ota_types.h
|       |   +-- ota_events.h
|       |
|       +-- src/
|           +-- ota_service.c
|           +-- ota_session.c
|           +-- ota_validator.c
|
+-- callbox/
    +-- ota_policy.c
    +-- ota_web_handler.c
    +-- ota_output_adapter.c
    +-- ota_boot_validator.c
```

Nếu repo không có `services/` hiện tại, agent MAY chọn vị trí tương đương, nhưng MUST giữ dependency direction.

---

## 21. Dependency Direction

Allowed:

```text
CallBox Product
      |
      v
OTA Service
      |
      v
Platform OTA
      |
      v
ESP-IDF
```

Not allowed:

```text
Platform OTA -> CallBox
OTA Service  -> WebUI
OTA Service  -> MQTT business
OTA Service  -> Tower GPIO
```

Source adapter MAY phụ thuộc transport tương ứng:

```text
ota_web_handler -> esp_http_server / portal adapter
ota_https_source -> esp_http_client
```

nhưng transport MUST chỉ đưa stream vào OTA Service.

---

## 22. Implementation Phases

### Phase 0 – Freeze Flash Architecture

Deliverables:

```text
final partitions.csv
flash map document
size/alignment proof
security compatibility proof
migration plan
```

No OTA feature coding trước khi Phase 0 pass.

### Phase 1 – Storage Foundation

Deliverables:

```text
platform_nvs multi-partition
nvs_cfg
nvs_runtime
migration
tests
```

### Phase 2 – Platform OTA

Deliverables:

```text
platform_ota
unit-testable wrapper
A/B primitives
rollback primitives
```

### Phase 3 – Generic OTA Service

Deliverables:

```text
state machine
session API
stream write
validation
events
failure handling
```

### Phase 4 – Rollback & Boot Validation

Deliverables:

```text
PENDING_VERIFY handling
mark valid
rollback
health integration
boot-loop tests
```

### Phase 5 – CallBox Adapter

Deliverables:

```text
ota_policy
ota_output_adapter
normal/recovery policy
```

### Phase 6 – WebUI AP/STA OTA

Deliverables:

```text
/api/ota/status
/api/ota/upload
/api/ota/install
/api/ota/discard
progress UI
AP hold during upload
STA upload
```

### Phase 7 – HTTPS Server OTA

Deliverables:

```text
ota_https_source
manifest/version
TLS verification
resume support if needed
```

### Phase 8 – Cancel 10 s OTA Trigger

Deliverables:

```text
5 s AP gesture preserved
6-10 s warning beeps
10 s OTA request
no accidental CANCEL
server OTA trigger
```

---

## 23. Testing Matrix

Agent triển khai MUST có test cho tối thiểu các trường hợp:

```text
valid WebUI upload
invalid .bin
wrong target image
image larger than slot
browser disconnect at 10%
browser disconnect at 90%
re-upload after failure
install staged image
power loss during inactive write
power loss before activation
new firmware boot success
new firmware watchdog reset
new firmware panic
rollback to previous slot
AP upload
STA upload
OTA rejected while mission active
OTA allowed in approved recovery mode
Secure Boot production image
unsigned/invalid production image rejected
NVS config preserved through OTA
sequence continuity preserved
```

---

## 24. Agent Rules

Mọi agent làm phần này MUST tuân thủ:

1. Không sửa business behavior hiện hữu nếu task chỉ liên quan OTA.
2. Không gọi trực tiếp `esp_ota_*` ngoài `platform_ota`.
3. Không điều khiển tower/buzzer trực tiếp từ OTA service.
4. Không hardcode CallBox mission policy trong generic OTA.
5. Không tạo OTA engine riêng cho mỗi source.
6. Không load toàn firmware vào RAM.
7. Không ghi đè running partition.
8. Không activate image trước khi `finish + validate` thành công.
9. Không coi download/upload thành công là OTA thành công.
10. Không bỏ rollback.
11. Không thay partition table production mà chưa có migration plan.
12. Không erase NVS khi migration lỗi chỉ để “boot cho được”.
13. Không log secrets.
14. Ưu tiên API chính thức ESP-IDF 6.1 và example chính thức của Espressif.
15. Trước khi code phase mới, đọc lại tài liệu này và kiểm tra dependency direction.

---

## 25. ESP-IDF Reference Baseline

Khi triển khai, agent SHOULD đối chiếu implementation với các nguồn chính thức Espressif:

```text
espressif/esp-idf
|
+-- components/app_update/
|   +-- esp_ota_ops.*
|
+-- examples/system/ota/native_ota_example/
|
+-- examples/system/ota/advanced_https_ota/
|
+-- docs/
    +-- OTA API
    +-- Partition Tables
    +-- Secure Boot
    +-- Flash Encryption
```

Có thể học architecture/lifecycle từ ESP RainMaker OTA, nhưng không kéo toàn bộ RainMaker framework vào CallBox nếu không có yêu cầu riêng.

---

## 26. Definition of Done – OTA Foundation

OTA foundation chỉ được coi đạt khi:

```text
16 MB partition map đã freeze
A/B slots hoạt động
rollback hoạt động
persistent config không mất
sequence không bị reuse
generic OTA session có thể nhận stream
WebUI AP và STA dùng cùng backend
image validation có trước activation
new image có post-boot validation
failure không brick thiết bị
production security vẫn giữ nguyên
generic OTA không phụ thuộc CallBox business
```

---

## 27. Architecture Decision Summary

Quyết định baseline:

```text
FLASH
    -> lifecycle-based partitioning

APPLICATION
    -> factory recovery + ota_0 + ota_1

CONFIG
    -> nvs_cfg

RUNTIME
    -> nvs_runtime

OTA PROVIDER
    -> platform_ota

OTA ENGINE
    -> generic OTA service

OTA INPUT
    -> source adapters

OTA OUTPUT
    -> event/state adapters

CALLBOX RULES
    -> product ota_policy

ROLLBACK
    -> mandatory

WEBUI AP/STA
    -> same OTA upload backend

SERVER OTA
    -> future source adapter

CANCEL 10 s
    -> future trigger only
```

---

# FINAL ARCHITECTURE INVARIANT

> **OTA là reusable service, không phải CallBox-specific feature.**

> **Nguồn firmware có thể thay đổi; OTA core không thay đổi.**

> **Cách hiển thị có thể thay đổi; OTA core không thay đổi.**

> **Product policy có thể thay đổi; Platform OTA và generic OTA core không thay đổi.**

> **Flash layout được freeze trước khi triển khai OTA thực tế.**

Đây là invariant mà mọi agent phải giữ trong toàn bộ quá trình phát triển.
