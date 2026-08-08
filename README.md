# Callbox SEWS (ESP32-S3)

Firmware cho hệ thống Callbox AGV tại nhà máy SEWS — board
**Waveshare ESP32-S3-POE-ETH-8DI-8DO**.

## Overview

Mỗi callbox cho phép nhân viên chuyền yêu cầu **đổi giỏ** hoặc **cấp giỏ rỗng**
cho hệ thống AGV bằng nút bấm, và giao tiếp với **WCS (Warehouse Control
System)** qua MQTT. Trạng thái hiển thị bằng đèn LED trên nút, đèn tower 3 màu
và còi phản hồi âm thanh.

**Kiến trúc:**
- 3 nút bấm chiếu sáng (Task 1 Đổi giỏ, Task 2 Cấp giỏ rỗng, Cancel/Hủy)
- Đèn tower 3 màu (Đỏ / Vàng / Xanh), còi (buzz)
- Giao tiếp MQTT với WCS qua Wi-Fi STA hoặc W5500 Ethernet
- Sequence số bền vững qua NVS để đảm bảo giao hàng đáng tin cậy
- Portal cấu hình web chạy trên SoftAP (không cần Internet)

## Khởi động & cấu hình

Mỗi thiết bị chạy cùng binary:
- **SoftAP `CALLBOX-<id>`** mở ngay khi khởi động, mật khẩu **trùng SSID**
  (ví dụ AP `CALLBOX-001` → pass `CALLBOX-001`); IP mặc định `192.168.65.204`
- Kết nối điện thoại → truy cập `http://192.168.65.204/` để mở trang cấu hình
- Thiết bị tự quét và kết nối (STA) vào profile Wi-Fi nhớ mạnh nhất có sẵn
- Khi STA ổn định ≥ 30 s, AP **tự tắt** (trừ khi có client kết nối / cửa sổ
  cấu hình còn mở / giữ nút Cancel 5 s để bật lại AP rescue)
- Tài khoản mặc định trang portal: `aubot` (admin / password web)

Nếu MQTT hoặc mạng mất, AP khôi phục tự bật để vào portal lại.

## File Structure

```
TungLamvsEsp32-S3/                # Thư mục gốc dự án
├── main/
│   ├── callbox_sews.c            # Điểm khởi động (app_main) — khởi tạo toàn bộ
│   ├── callbox_io.c/h            # ⭐ Bảng ánh xạ I/O (đấu dây thật) — chỉ sửa file này nếu đấu lại dây
│   ├── io_handler.c/h            # Task đọc nút, debounce, phát sự kiện cạnh
│   ├── button_gate.c/h           # Cổng chặn cạnh nhấn/thả (chống nhấn lặp)
│   ├── state_machine.c/h         # Mission Manager — chủ duy nhất trạng thái nghiệp vụ
│   ├── callbox_mqtt.h            # Đề cương MQTT: topic, task communication
│   ├── mqtt_client.c             # Phân lớp ESP-MQTT (pub queue, reconnect, TLS)
│   ├── protocol_types.c/h        # JSON codec cho lệnh từ WCS (accepted/assigned/...)
│   ├── sequence_service.c/h      # Cấp số seq cá nhân (retry cùng seq)
│   ├── output_renderer.c/h       # Render snapshot → LED/tower/buzzer
│   ├── status.c/h                # Snapshot trạng thái liên tầng (mirror WCS)
│   ├── app_event_queue.c/h       # Event queue nội bộ (WCS cmd, MQTT kết nối/ngắt)
│   ├── network_status_task.c/h   # LED AP + bíp STA lên/xuống + chính sách tắt AP
│   ├── wifi_init.c/h             # APSTA, scan/lock, cấu hình IP tĩnh, rescue AP
│   ├── nvs_storage.c/h           # Cấu hình Wi-Fi/MQTT bền vững
│   ├── time_sync.c/h             # SNTP (pool.ntp.org / time.google.com)
│   ├── config_portal.c/h         # Portal HTTP cấu hình (ID, Wi-Fi, MQTT, I/O)
│   ├── io_debug.c/h              # Tiện ích I/O cho portal /api/io-status
│   └── led_control.c/h           # Buzzer LED queue, hiệu ứng blink
├── components/bsp/               # Board Support Package (nút, DO, 8DI, buzzer)
├── img/company-logo-transparent.png   # Logo nhúng cho trang portal
├── partitions.csv                # factory 2 MB (NVS 24 KB + phy 4 KB)
└── tools/mqtt_broker_test.py     # Script kiểm thử MQTT cho broker
```

## Giao thức MQTT

Transport được ESP-MQTT đảm nhận (framing, keep-alive 30 s, auto-reconnect,
TLS/CA bundle khi chọn mode `mqtts`). QoS = **1**. Heartbeat mỗi **15 giây**.
Client ID: `AUBOT-Callbox-<id>`.

| Topic | Chiều | Nội dung |
|-------|-------|----------|
| `callbox/{id}/event` | Out | `call`, `cancel`, `sync_request` |
| `callbox/{id}/cmd` | In | `accepted`, `assigned`, `locked`, `completed`, `cancel_ack`, `rejected`, `overdue`, `sync`, `config` |
| `callbox/{id}/status` | Out (retained) | Status + heartbeat |

### Call event (button nhấn)
```json
{"type":"call","task":1,"seq":1042,"ts":1751791860}
```

### Cancel event
```json
{"type":"cancel","task":1,"seq":1043,"ts":1751791861}
```

### Sync request (reconstruct trạng thái sau khi mất MQTT)
```json
{"type":"sync_request","seq":1044,"ts":1751791862,"fw":"1.2.0"}
```

### WCS → `accepted` / `assigned` / `locked`
```json
{"type":"assigned","task":1,"ref_seq":1042,"agv_id":"agv03","ts":1751791865}
```

### Rejected với reason có cấu trúc
```json
{"type":"rejected","task":1,"ref_seq":1042,"reason":"locked","ts":1751791870}
```
Reason hợp lệ: `locked`, `duplicate`, `no_task`, `wcs_busy` (không biết → `none`).

### Heartbeat/Status (retained)
```json
{
  "online": true, "comm": "ready",
  "task1": "assigned", "task2": "idle",
  "rssi": -55, "uptime": 86214,
  "time_synced": true, "fw": "1.2.0", "ts": 1751791920
}
```
- `comm`: `offline` | `syncing` | `ready` — sau khi reconnect, thiết bị gửi
  `sync_request` và **không nhận nút bấm** cho đến khi WCS gửi `sync` trả về
  trạng thái 2 task.
- Khi mất mạng bất thường, last will với `{"online":false}` retained sẽ được
  publish tự động.

## Nút bấm & trạng thái

| Nút | Task | Màu LED nút | Hành động |
|-----|------|-------------|-----------|
| Button 1 | Task 1 | Vàng | Đổi giỏ (full → empty) |
| Button 2 | Task 2 | Xanh | Cấp giỏ rỗng |
| Button 3 | Cancel | Đỏ | Hủy task đang chờ/đã gán; giữ 5 s = bật/tắt AP khôi phục |

### Chu trình task (do WCS điều khiển)

```
IDLE → (button) → QUEUED (WCS accepted)
                 → ASSIGNED (WCS gán AGV)
                 → LOCKED (AGV đã lấy giỏ, cancel hết hiệu lực)
                 → COMPLETED (về IDLE)
```

Callbox **chỉ thay đổi trạng thái task khi WCS gửi lệnh**: bấm nút chỉ gửi
sự kiện và mở khóa `pending`; chậm trễ/quá hạn có `retry` (mỗi 5 s, tối đa
2 lần) và timeout hiển thị lỗi + bíp dài.

### Đèn LED (bấm phím ↔ task state but code thực tế đảo hành)

| Trạng thái | LED nút | Tower |
|------------|---------|-------|
| Đợi lệnh / mất MQTT | - | Đỏ nhấp nháy chậm (comm `syncing`/`offline`) |
| Bấm nút, chờ thủ công | Blink chậm | Vàng |
| WCS gán/phê duyệt | Sáng đều | Vàng (task active) |
| Task hoàn thành | Tắt | Xanh (rảnh) |
| Lỗi / rejected | Blink nhanh 3 nhịp | Đỏ sáng |
| Overdue | - | Vàng nhấp nháy chậm |
| Hủy được xác nhận | Cancel LED nhấp nháy nhanh 0,7 s | - |

> `comm != ready` → tower đỏ (blink nhanh) — thiết bị không thể hoạt động tới
> khi WCS đồng bộ xong.
>
> Cancel giữ ≥ 5 s bật/tắt **AP khôi phục** (3 bíp xác nhận), lock cả Cancel
> để không gửi nhầm lệnh hủy trong lúc rescue.

## Cấu hình (portal)

- Đăng nhập trang web: tài khoản `aubot` / mật khẩu web (`web_password`,
  mặc định `aubot`)
- **Scan WiFi** → chọn mạng/cập tình hình, nhập mật khẩu, lưu. Profile mới
  được đưa lên slot ưu tiên cao nhất; tối đa **5 profile** nhớ trong NVS.
- **MQTT**: broker (host hoặc IP), port (mặc định `1884`), username/password
  tùy chọn, mode **TCP** (nội bộ) hoặc **TLS** (CẢNH BÁO: TLS cần SNTP có
  thời gian hợp lệ trước khi kết nối).
- **IP tĩnh**: tùy chọn thay DHCP cho STA.
- ID callbox: dạng số (ví dụ `001`, `cb01` cũ được tự chuyển thành số `001`).
- Thay đổi áp dụng ngay không cần reboot lại; AP khôi phục vẫn mở khi cần.

## Building & Flashing

### Prerequisites
- ESP-IDF **v6.1** (framework cũ hơn v5.3 không promise build)
- Waveshare ESP32-S3-POE-ETH-8DI-8DO
- Python 3.7+

### Build
```bash
idf.py build
```

### Flash
```bash
idf.py -p <port> flash monitor     # ví dụ COM3 hoặc /dev/ttyUSB0
```

### Menuconfig
```bash
idf.py menuconfig
```

## Hardware Setup

### GPIO Mapping (đấu dây WSP thực tế)

| Thành phần | GPIO/Kênh | Chức năng |
|------------|-----------|-----------|
| DI1 | GPIO4 | Nút cancel (active low) |
| DI2 | GPIO5 | Nút task 2 (active low) |
| DI3 | GPIO6 | Nút task 1 (active low) |
| DI4..DI8 | GPIO7..11 | Dự phòng / chưa dùng |
| I2C SCL/SDA | GPIO41/GPIO42 | TCA9554PWR (địa chỉ `0x20`) |
| DO1 buzzer | TCA P0 | Buzzer (active low) |
| DO2..DO4 tower | TCA P1..P3 | Đỏ / Vàng / Xanh |
| DO5..DO7 LED nút | TCA P4..P6 | Cancel=DO5, Task 2=DO6, **Task 1=DO7** |
| DO8 | TCA P7 | LED trạng thái AP |
| W5500 SPI | GPIO12..16, GPIO39 | INT/MOSI/MISO/SCLK/CS/RESET |
| Buzzer onboard | GPIO46 | PWM BSP |

> **Nếu đấu lại dây board: CHỈ sửa `main/callbox_io.c`** — các module khác
> tra cứu qua hàm `callbox_io_get_mapping()`.

### Power Supply

- Input 24VDC (cho LED, tower, buzzer)
- Buck converter 24V → 5V/3.3V cho ESP32
- Bảo vệ: polar + fuse

## Troubleshooting

| Triệu chứng | Cách xử lý |
|-------------|-------------|
| Không vào được portal | Kiểm tra AP `CALLBOX-<id>` (pass = SSID AP); IP `192.168.65.204` |
| Mất nút bấm | Bấm nút nhưng không đèn — xem `idf.py monitor`, kiểm tra GPIO/pull-up |
| MQTT không liên lạc | Thử: `mosquitto_sub -h <broker> -p 1884 -t "callbox/+/status"` |
| LED không hoạt động | Kiểm tra 24V nguồn, đấu dây TCA9554, điện áp DO |
| Task không về IDLE | WCS chưa gửi `completed` — sync lại qua `sync_request` |
| AP bị tự tắt | STA ổn định > 30 s là chính sách bình thường; giữ Cancel 5 s để mở lại |
| TLS lỗi cert | Cần SNTP thời gian thực trước (xem log `time_sync`) |

## Cấu trúc kho & công cụ

```
docs/                       # Tài liệu giao thức MQTT + phương án AGV (docx)
tools/mqtt_broker_test.py   # python script kiểm thử broker/local
build-win/                  # bản build cũ dùng Windows (có sdkconfig.old...)
```

## References

- [Giao tiep MQTT TCP Bo goi WCS.docx](docs/Giao%20tiep%20MQTT%20TCP%20Bo%20goi%20WCS.docx)
- [Phuong_an_Bo_goi_AGV_SEW_20260706.docx](docs/Phuong_an_Bo_goi_AGV_SEW_20260706.docx)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/)

## License

Internal AUBOT Project – SEWS Manufacturing Facility