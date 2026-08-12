# MQTT contract trạng thái kho V1

Tài liệu này là contract tích hợp giữa Gateway cảm biến Laser và FMS/WCS. Cặp
topic trạng thái được thiết kế đồng nhất với hệ thống Vision; chỉ
`source_type` và nhánh nguồn trong topic khác nhau.

## Namespace và định danh

Gateway cảm biến dùng namespace:

```text
warehouse/sensor/{company_id}/{site_id}/{warehouse_id}
```

Ví dụ:

```text
warehouse/sensor/aubot/ha-noi/kho-vp
```

Ý nghĩa các thành phần:

- `company_id`: mã công ty hoặc chủ dự án, ví dụ `aubot`.
- `site_id`: mã địa điểm triển khai, ví dụ `ha-noi`.
- `warehouse_id`: mã kho ổn định dùng để tích hợp, ví dụ `kho-vp`.
- `warehouse_name`: tên hiển thị, ví dụ `Kho Văn Phòng`; không nằm trong topic.
- `gateway_id`: định danh thiết bị cho AP, MQTT client ID và chẩn đoán; không nằm
  trong topic trạng thái kho.

Mỗi segment `company_id`, `site_id`, `warehouse_id` phải dài từ 1 đến 31 ký tự,
bắt đầu bằng chữ thường ASCII hoặc số, và các ký tự còn lại chỉ được là
`a-z`, `0-9`, `-`, `_`. Không chấp nhận chữ hoa, dấu tiếng Việt, khoảng trắng,
`/`, `+` hoặc `#`.

`warehouse_id` phải được xem là khóa tích hợp ổn định. Đổi `warehouse_name`
không đổi topic. Đổi `warehouse_id` làm thay đổi toàn bộ topic của kho và cần
phối hợp với FMS trước khi thực hiện.

Mỗi bộ `{company_id, site_id, warehouse_id}` chỉ có một publisher sở hữu. Nếu
nhiều Gateway cùng phục vụ một kho, hệ thống phải có một tầng aggregator duy
nhất thay vì để nhiều thiết bị publish cùng retained topic.

## Hai topic ngoài duy nhất

| Topic sau namespace | QoS | Retain | Hướng | Mục đích |
|---|---:|---:|---|---|
| `status/json` | 1 | Có | Gateway → FMS | Snapshot đầy đủ dạng JSON |
| `status/bits` | 1 | Có | Gateway → FMS/PLC | Cùng snapshot dưới dạng bit ASCII |

Đây là toàn bộ contract MQTT bên ngoài của Gateway Sensor V1. Gateway không publish
`event`, không subscribe `cmd` và không dùng topic `availability`/Last Will riêng.
FMS xác định dữ liệu còn mới từ thời điểm nhận snapshot và `generated_at`, cùng cách
vận hành với nguồn Vision.

Hai topic trạng thái đầy đủ của ví dụ trên là:

```text
warehouse/sensor/aubot/ha-noi/kho-vp/status/json
warehouse/sensor/aubot/ha-noi/kho-vp/status/bits
```

Gateway tạo JSON và bits từ cùng một snapshot, sau đó publish cả hai với QoS 1
và retained. `sequence` chỉ tăng một lần cho cả cặp. QoS 1 có thể tạo bản tin
lặp; consumer phải xử lý idempotent.

## Thứ tự và mã hóa trạng thái

Snapshot luôn chứa đủ số vị trí của profile đang dùng:

- profile 8 vị trí: 8 state và 16 ký tự bit;
- profile 12 vị trí: 12 state và 24 ký tự bit.

Thứ tự cố định là `left_to_right_top_to_bottom`, tức từ trái sang phải rồi từ
trên xuống dưới. Không loại bỏ vị trí chưa cấu hình.

Mỗi vị trí dùng hai bit:

| Trạng thái | Bit | Ý nghĩa |
|---|---:|---|
| `EMPTY` | `00` | Có dữ liệu hợp lệ và không có hàng |
| `OCCUPIED` | `01` | Có dữ liệu hợp lệ và có hàng |
| `UNKNOWN` | `10` | Chưa cấu hình/tắt vị trí, hoặc chưa có dữ liệu hợp lệ |
| `FAULT` | `11` | Vị trí đã cấu hình nhưng Laser đang offline |

Ví dụ:

```text
[EMPTY, OCCUPIED, UNKNOWN, FAULT] -> 00011011
```

Payload của `status/bits` là chuỗi ASCII `0`/`1` thô. Nó không phải JSON string,
không có dấu nháy, newline hoặc byte NUL ở cuối.

## Payload `status/json`

```json
{
  "schema": "WAREHOUSE_STATUS_V1",
  "source_type": "sensor",
  "company_id": "aubot",
  "site_id": "ha-noi",
  "warehouse_id": "kho-vp",
  "warehouse_name": "Kho Văn Phòng",
  "slot_count": 4,
  "order": "left_to_right_top_to_bottom",
  "state_bits": "00011011",
  "states": [
    "EMPTY",
    "OCCUPIED",
    "UNKNOWN",
    "FAULT"
  ],
  "occupied_count": 1,
  "empty_count": 1,
  "unknown_count": 1,
  "fault_count": 1,
  "sequence": 125,
  "layout_version": "87b865b9cd93",
  "generated_at": "2026-08-12T10:20:30.000000Z"
}
```

Trong firmware thật, `slot_count` chỉ là 8 hoặc 12. Ví dụ bốn vị trí ở trên chỉ
để minh họa đủ bốn mã trạng thái.

Quy tắc bắt buộc:

- `schema` luôn là `WAREHOUSE_STATUS_V1`.
- `source_type` của Gateway Laser luôn là `sensor`; hệ thống Camera dùng
  `vision` trên nhánh `warehouse/vision/...`.
- `states.length == slot_count`.
- `state_bits.length == slot_count * 2` và phải mã hóa chính xác mảng `states`.
- Tổng bốn biến đếm bằng `slot_count`.
- `sequence` tăng theo từng snapshot đã publish. Consumer không được dùng riêng
  nó làm định danh toàn cục qua mọi lần khởi động.
- `layout_version` gồm 12 ký tự hex và chỉ đổi khi layout/mapping vị trí đổi;
  trạng thái realtime, tên hiển thị và identity topic không làm đổi giá trị này.
- `generated_at` là UTC RFC 3339 khi đồng hồ hệ thống hợp lệ; có thể là `null`
  trước khi Gateway có thời gian hợp lệ. Khi có giá trị, timestamp luôn mang phần
  giây thập phân sáu chữ số theo cùng hình dạng với payload Vision, ví dụ
  `2026-08-12T10:20:30.382663Z`.

Thứ tự field của JSON cũng thống nhất với Vision: `schema`, `source_type`, ba field
identity, `warehouse_name`, `slot_count`, `order`, `state_bits`, `states`, bốn bộ
đếm theo thứ tự `occupied`, `empty`, `unknown`, `fault`, rồi `sequence`,
`layout_version`, `generated_at`. Consumer vẫn nên parse theo tên field thay vì phụ
thuộc vào thứ tự byte.

`status/json` không thêm field riêng của thiết bị như `gateway_id`, `boot_id`,
CAN counter hoặc thông số Laser. Các dữ liệu chẩn đoán này thuộc WebUI/API kỹ
thuật, không thuộc snapshot chung Vision/Sensor.

## Subscribe wildcard

```text
# Một payload đầy đủ của một kho
warehouse/sensor/aubot/ha-noi/kho-vp/status/json

# Cả JSON và bits của một kho
warehouse/sensor/aubot/ha-noi/kho-vp/status/+

# Tất cả kho cảm biến tại Hà Nội của AUBOT
warehouse/sensor/aubot/ha-noi/+/status/json

# Tất cả nguồn, công ty, địa điểm và kho
warehouse/+/+/+/+/status/json
```

## Lưu ý vận hành và migration

- Firmware không dual-publish topic legacy `gateway/{gateway_id}/...`.
- Khi identity đổi, Gateway dùng namespace mới. Retained status ở namespace cũ
  không tự bị xóa; quản trị broker/FMS chịu trách nhiệm dọn sau khi migration.
- Các bản firmware trước từng tạo retained topic
  `warehouse/sensor/{company_id}/{site_id}/{warehouse_id}/availability`. Firmware
  mới gửi đúng một retained payload rỗng khi kết nối để broker xóa giá trị cũ. Đây
  chỉ là thao tác dọn migration, không phải topic thứ ba của contract và không có
  payload `online/offline` mới.
- Gateway không nhận lệnh cấu hình Laser qua MQTT. Cấu hình vận hành thực hiện qua
  WebUI/API có phân quyền.
- Gateway không lưu và phát lại lịch sử. FMS/WCS sở hữu phần lịch sử dữ liệu.

## Kiểm tra bằng bench client

Credential không truyền trên command line. Đặt chúng trong biến môi trường:

```powershell
$env:MQTT_BENCH_USERNAME = '<user>'
$env:MQTT_BENCH_PASSWORD = '<password>'
python tools/mqtt_bench_client.py wcs.example.vn `
  --company-id aubot --site-id ha-noi --warehouse-id kho-vp `
  --verify-status-pair --duration 30
```

Chế độ `--verify-status-pair` subscribe `status/+`, kiểm tra schema, identity,
số vị trí, bảng mã, bộ đếm và yêu cầu `state_bits` trong JSON trùng tuyệt đối
payload raw bits. Công cụ trả exit code khác 0 nếu không nhận được một cặp hợp lệ
hoặc phát hiện sai contract.
