# CallBox SEWS — ESP32-S3

Firmware ESP-IDF cho **Waveshare ESP32-S3-POE-ETH-8DI-8DO**. CallBox nhận nút vật lý, giao tiếp WCS qua MQTT và điều khiển LED/tháp đèn/còi.

## Nghiệp vụ
- **Task 1 — Exchange Cart:** trả xe đầy và yêu cầu xe rỗng.
- **Task 2 — Supply Empty Cart:** yêu cầu cấp xe rỗng.
- Hai task độc lập; WCS là nguồn trạng thái authoritative.
- **Cancel:** chỉ hủy task `queued`/`assigned` có `CallSequence` mới nhất; `locked` không hủy từ CallBox.

## Kiến trúc
```text
main/app_main → callbox_app_run
                    ├─ CallBox: product/runtime, MQTT, portal, policy, NVS
                    ├─ BSP: board Waveshare → TCA9554 driver + ESP-IDF
                    └─ Platform: Wi‑Fi / NVS / time → ESP-IDF
```
Hướng dependency: `main → callbox`; `callbox → bsp/platform`; `bsp → driver/ESP-IDF`; `platform → ESP-IDF`. Cấm Platform → CallBox, BSP → CallBox, Driver → BSP và CallBox → main.

## Cây nguồn
```text
main/                       entrypoint ESP-IDF mỏng
components/callbox/         product/runtime CallBox
components/bsp/             phần cứng Waveshare
components/drivers/tca9554/ driver IC generic
components/platform/        adapter Wi‑Fi, NVS, SNTP
docs/                       tài liệu WCS tham chiếu
tools/                      MQTT broker test cục bộ
```
Đọc sâu: [main](main/README.md), [components](components/README.md), [CallBox](components/callbox/README.md), [BSP](components/bsp/README.md), [drivers](components/drivers/README.md), [platform](components/platform/README.md), [docs](docs/README.md), [tools](tools/README.md).

## I/O vật lý
| Kênh | GPIO/TCA | Vai trò |
|---|---|---|
| DI1/GPIO4 | — | Cancel |
| DI2/GPIO5 | — | Task 2 |
| DI3/GPIO6 | — | Task 1 |
| DI4–DI8 | — | Chưa dùng |
| DO1/P0 | TCA9554 | Còi nghiệp vụ |
| DO2/P1, DO3/P2, DO4/P3 | TCA9554 | Tháp đỏ, vàng, xanh |
| DO5/P4, DO6/P5, DO7/P6 | TCA9554 | LED Cancel, Task2, Task1 |
| DO8/P7 | TCA9554 | LED AP |
| GPIO46 | onboard | Còi báo mạng |
Thay đổi ý nghĩa thiết bị tại `components/callbox/src/callbox_io.c`; raw board wiring thuộc BSP.

## MQTT/WCS
Topics QoS 1: `callbox/{id}/event`, `callbox/{id}/cmd`, `callbox/{id}/status`. Client ID là `AUBOT-Callbox-<id>`; heartbeat 15 s, keepalive 30 s, LWT retained `{"online":false}`. MQTT reconnect luôn yêu cầu sync; button chỉ nhận call khi matching sync đưa Comm về `ready`.

## Mạng, AP và portal
- Network link = Wi‑Fi STA **OR** W5500 Ethernet; Ethernet-only được hỗ trợ.
- Tối đa 5 Wi‑Fi đã nhớ; chọn SSID nhìn thấy mạnh nhất.
- AP: SSID `CALLBOX-<id>-<MAC6>`; password `CALLBOX-<id>`; fallback SSID không suffix nếu đọc MAC lỗi.
- AP `192.168.65.204/24`, channel 1, tối đa 4 client.
- Giữ Cancel ≥5 s toggle Rescue AP. AP tự tắt khi STA ổn định ≥30 s, rescue tắt, không client và portal inactive.
- Portal STA: username `admin`, password factory `aubot`; AP subnet có bypass cứu hộ.

## Build / flash
ESP-IDF đã xác nhận: `v6.1-dev`, ESP32-S3, GCC 15.2.0.
```powershell
$env:PYTHONPATH = "$PWD\.vscode\python"
$env:PROCESSOR_ARCHITECTURE = 'AMD64'
. 'C:\Espressif\tools\Microsoft.v6.1-dev.PowerShell_profile.ps1'
idf.py set-target esp32s3
idf.py -B build-win build
idf.py -B build-win -p COM18 flash
idf.py -B build-win -p COM18 monitor
```
Baseline source `6493d671`: bin `0x119B30` (1,153,840 bytes), partition `0x200000`, free ~45%. Nếu Windows parallel build gặp GCC ICE trong ESP-IDF `esp_lcd`, dùng `ninja -C build-win -j1`; đây không phải source defect CallBox.

## Validation / nợ kỹ thuật
P0/P1 = 0. Còn P2/P3: sequence init mutex cleanup, AP-start lifecycle HW test, MQTT config snapshot, status multi-field snapshot, rescue-beep mailbox, legacy/dead comments. Hardware acceptance vẫn phải test DI/DO, Wi‑Fi/Ethernet, portal, Rescue AP, MQTT TCP/TLS, Task coexist/cancel/locked/reject/overdue/persistence và static→DHCP không reboot. Đổi credential factory trước production.
