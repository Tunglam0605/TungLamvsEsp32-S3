# CallBox SEWS — ESP32-S3

Firmware ESP-IDF cho Waveshare ESP32-S3-POE-ETH-8DI-8DO. CallBox nhận nút vật lý, điều khiển tháp đèn/còi và giao tiếp WCS qua MQTT.

## Mục đích nghiệp vụ

- Task 1 — Exchange Cart: trả xe đầy và yêu cầu xe rỗng.
- Task 2 — Supply Empty Cart: yêu cầu cấp xe rỗng.
- Hai task độc lập. WCS là nguồn trạng thái authoritative.
- Cancel chỉ nhắm task queued/assigned có CallSequence mới nhất; locked không hủy từ CallBox.

## Kiến trúc hệ thống

    Operator → CallBox → MQTT Broker → WCS
                   │
                   ├─ Wi-Fi STA hoặc W5500 Ethernet
                   └─ Web portal cục bộ để commissioning

Portal không phải production API của WCS. Đặc tả cho IT/WCS: [WCS / IT MQTT Interface Specification](docs/WCS_MQTT_INTERFACE.md).

## Kiến trúc repository

    main/app_main → callbox_app_run
                       ├─ CallBox: product policy, Mission, MQTT, portal
                       ├─ BSP: board Waveshare
                       │   └─ driver TCA9554
                       └─ Platform: Wi-Fi, NVS, time

Dependency chỉ đi xuống: main → CallBox → BSP/Platform; BSP → driver/ESP-IDF; Platform → ESP-IDF. CallBox không phụ thuộc main; BSP/Platform không biết nghiệp vụ CallBox.

## Hardware mapping

| Kênh | Mapping | Vai trò |
|---|---|---|
| DI1 / GPIO4 | Input active-low | Cancel |
| DI2 / GPIO5 | Input active-low | Task 2 |
| DI3 / GPIO6 | Input active-low | Task 1 |
| DI4–DI8 | — | Chưa dùng |
| DO1 / TCA P0 | Output active-low | Còi nghiệp vụ |
| DO2 / P1, DO3 / P2, DO4 / P3 | — | Tháp đỏ, vàng, xanh |
| DO5 / P4, DO6 / P5, DO7 / P6 | — | LED Cancel, Task2, Task1 |
| DO8 / P7 | — | LED AP |
| GPIO46 | On-board | Còi feedback mạng |

Ý nghĩa nghiệp vụ I/O nằm tại components/callbox/src/callbox_io.c; wiring/driver là trách nhiệm BSP.

## Runtime boot

Bootstrap khởi tạo NVS/config/sequence/queues/BSP/I-O/Mission, sau đó Wi-Fi/AP, SNTP, network status, Ethernet, chờ 5 giây và khởi tạo MQTT/tasks. Khi MQTT kết nối, CallBox vào syncing, gửi sync_request; chỉ sync đúng mới đưa Comm về ready để nhận thao tác cục bộ.

## Mission và MQTT/WCS

CALL chỉ tạo transaction pending và LED nháy chậm. matching accepted mới chuyển Mission queued; assigned/locked/completed do WCS quyết định. Retry giữ nguyên seq, vì vậy WCS phải deduplicate.

Topics QoS 1:

    callbox/{id}/event   CallBox → WCS
    callbox/{id}/cmd     WCS → CallBox
    callbox/{id}/status  CallBox → WCS, retained

Client ID là AUBOT-Callbox-{id}; heartbeat 15 giây, keepalive 30 giây, LWT retained là {"online":false}. JSON, correlation seq/ref_seq, retry, sync và trách nhiệm backend nằm trong [đặc tả MQTT/WCS](docs/WCS_MQTT_INTERFACE.md).

## Network, AP và portal

- Uplink là Wi-Fi STA hoặc W5500 Ethernet; MQTT không đổi theo uplink.
- Lưu tối đa năm Wi-Fi profile và chọn SSID nhìn thấy có RSSI mạnh nhất.
- AP: CALLBOX-{id}-{MAC6}; password CALLBOX-{id}; IP 192.168.65.204/24.
- Giữ Cancel tối thiểu 5 giây để bật/tắt Rescue AP.
- AP tự tắt khi STA ổn định ít nhất 30 giây, rescue tắt, không có AP client và portal inactive.
- Portal qua STA hoặc AP: username admin, mật khẩu aubot. Firmware hiện ép mật khẩu này ở mỗi lần boot, nên chưa hỗ trợ mật khẩu portal tùy chỉnh bền vững.

Đổi credential factory trước production.

## Build và flash

Lệnh chính, không phụ thuộc máy cụ thể:

    idf.py set-target esp32s3
    idf.py build
    idf.py -p <PORT> flash
    idf.py -p <PORT> monitor

### Validated Windows development environment

Đã xác nhận với ESP-IDF v6.1-dev và GCC 15.2.0. Thư mục build đã dùng trong máy phát triển có thể đặt build-win:

    idf.py -B build-win build
    idf.py -B build-win -p COM18 flash

COM18 chỉ là ví dụ, thay bằng cổng thiết bị thực tế. Nếu parallel build gặp GCC ICE bên trong ESP-IDF, chạy ninja -C build-win -j1.

## Validation state

Firmware functional baseline: H.2.1 là 88669b8a35de7968d55a215d3102af5b049cdf0a; portal authentication hiện tại là 77fb093e03ed891eff27083affe2f5d6d9eb71c1. Commit tài liệu không làm thay đổi binary.

- H.2 provider DHCP: 6493d671 — provider khởi động lại DHCP khi runtime static → DHCP.
- H.2.1 CallBox mapping: 88669b8 — portal save → wifi_apply_config → configure_sta_ip → platform_wifi_apply_sta_network_config cho cả DHCP và static.
- Build source đã pass: 1,153,840 byte (0x119B30), app partition 0x200000, còn khoảng 45%.
- Commit tài liệu không làm thay đổi binary.

Runtime static → DHCP phần cứng vẫn **NOT RUN**; không được coi là hardware acceptance PASS.

## Documentation map

- [main](main/README.md) — ESP-IDF entrypoint.
- [components](components/README.md) — dependency layers.
- [CallBox](components/callbox/README.md) — engineering/runtime guide.
- [BSP](components/bsp/README.md), [drivers](components/drivers/README.md), [platform](components/platform/README.md).
- [docs](docs/README.md) — tài liệu handoff; [WCS MQTT interface](docs/WCS_MQTT_INTERFACE.md).
- [tools](tools/README.md) — broker test cục bộ.

## Known nonblocking debt

P0 = 0, P1 = 0. P2/P3 còn: sequence init mutex cleanup, AP-start lifecycle/hardware validation, MQTT runtime config snapshot concurrency, multi-field status snapshot consistency, rescue notification mailbox đơn slot, legacy/dead comments/APIs.

## Hardware acceptance outstanding

Trước nghiệm thu cần test DI/DO, Wi-Fi/Ethernet, portal/Rescue AP, MQTT TCP/TLS, đồng thời Task1+Task2, cancel/locked/rejected/overdue, persistence và toàn bộ ma trận DHCP không reboot.
