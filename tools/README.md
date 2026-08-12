# Công cụ Gateway

## MQTT bench client

`mqtt_bench_client.py` là MQTT 3.1.1 client không cần thư viện ngoài. Credential
được đọc từ `MQTT_BENCH_USERNAME` và `MQTT_BENCH_PASSWORD`, không đặt trực tiếp
trên command line hoặc commit vào repository.

Kiểm tra cặp retained JSON/bits của một kho:

```powershell
$env:MQTT_BENCH_USERNAME = '<user>'
$env:MQTT_BENCH_PASSWORD = '<password>'
python tools/mqtt_bench_client.py wcs.example.vn `
  --company-id aubot --site-id ha-noi --warehouse-id kho-vp `
  --verify-status-pair --duration 30
```

Chế độ này tự subscribe:

```text
warehouse/sensor/aubot/ha-noi/kho-vp/status/+
```

Nó kiểm tra schema `WAREHOUSE_STATUS_V1`, identity theo topic, 8/12 vị trí,
bảng mã hai bit, thứ tự field đồng bộ Vision, timestamp UTC RFC 3339 có phần giây
thập phân, bộ đếm và `state_bits` JSON trùng raw bits. Exit code khác 0 nếu không
nhận được cặp hợp lệ hoặc có lỗi contract.

Contract ngoài chỉ có hai topic retained `status/json` và `status/bits`; công cụ
không gửi `cmd` và không chờ `event` hoặc `availability`.

Subscribe tùy ý hoặc publish payload thủ công vẫn được hỗ trợ bằng `--topic` và
`--publish TOPIC=PAYLOAD`. Xem toàn bộ tùy chọn bằng:

```powershell
python tools/mqtt_bench_client.py --help
```

Contract chi tiết: [docs/GATEWAY_MQTT_CONTRACT_V1.md](../docs/GATEWAY_MQTT_CONTRACT_V1.md).

## HTTP soak

`gateway_http_soak.ps1` poll các API trạng thái để thu log soak local. Kết quả
chỉ là bằng chứng khi ghi rõ firmware/commit, thời lượng, phần cứng và raw log;
không dùng báo cáo bench cũ để đại diện cho firmware mới.
