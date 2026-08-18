# Callbox ↔ WCS: hướng dẫn kiểm thử MQTT cho đội IT

Tài liệu này mô tả phần việc phía IT/WCS để kiểm thử hai thiết bị Callbox
đã được cấu hình trên broker. Đây là giao tiếp MQTT 3.1.1, payload JSON và
QoS 1.

## 1. Thông số kết nối

| Mục | Giá trị |
|---|---|
| Broker | `wcs.aubot.vn` |
| Cổng | `1883` |
| Transport | MQTT TCP thường, **không phải TLS** |
| Tài khoản | `fms` |
| Mật khẩu | Mật khẩu do IT đã cấp cho tài khoản `fms` |
| QoS | `1` cho event, command và status |
| Client ID Callbox | `AUBOT-Callbox-001`, `AUBOT-Callbox-002` |

> Không thêm `mqtt://` vào trường host của Callbox. Cổng 1883 không mã hóa
> TLS; nếu cần TLS, IT cần cấp endpoint TLS riêng (thường là 8883) và CA.

## 2. Phân quyền MQTT cần cấp

Tài khoản của Callbox phải có quyền sau, thay `{id}` bằng ID thiết bị:

| Hướng | Quyền | Topic |
|---|---|---|
| Callbox → WCS | Publish | `callbox/{id}/event` |
| Callbox → WCS | Publish | `callbox/{id}/status` |
| WCS → Callbox | Publish (Callbox subscribe) | `callbox/{id}/cmd` |

Để test đồng thời các thiết bị, ACL wildcard có thể dùng:

```text
callbox/+/event
callbox/+/status
callbox/+/cmd
```

Command từ WCS phải publish **không retain**. Status của Callbox là retained,
vì vậy client mới subscribe sẽ nhận được trạng thái cuối cùng ngay lập tức.

## 3. Trình tự bắt buộc sau khi Callbox lên broker

Khi kết nối MQTT thành công, Callbox publish một `sync_request`:

```text
Topic: callbox/002/event
QoS:   1
```

```json
{"type":"sync_request","seq":50,"ts":1786420790,"fw":"1.2.0"}
```

Trong khi chưa nhận `sync`, Callbox publish status có:

```json
{"online":true,"comm":"syncing","task1":"idle","task2":"idle"}
```

WCS phải phản hồi ngay về đúng topic command và dùng **đúng `ref_seq` bằng
`seq` của `sync_request` gần nhất**:

```text
Topic: callbox/002/cmd
QoS:   1
Retain: false
```

```json
{
  "type": "sync",
  "ref_seq": 50,
  "task1_state": "idle",
  "task1_seq": 0,
  "task1_agv_id": "",
  "task2_state": "idle",
  "task2_seq": 0,
  "task2_agv_id": "",
  "ts": 1786420790
}
```

Sau khi nhận sync đúng, Callbox chuyển `comm` thành `ready` và mới nhận thao
tác nút. Nếu WCS bỏ qua hoặc phản hồi sai `ref_seq`, Callbox vẫn ở `syncing`.

Với Callbox `001`, thay topic thành `callbox/001/event` và
`callbox/001/cmd`; dùng `ref_seq` đúng bằng `seq` mà thiết bị `001` đang gửi.

## 4. Kiểm thử vòng đời nhiệm vụ

### 4.1 Nhấn nút nhiệm vụ

Khi người vận hành nhấn Task 1, Callbox gửi:

```text
Topic: callbox/001/event
```

```json
{"type":"call","task":1,"seq":123,"ts":1786420800}
```

`seq` là mã giao dịch duy nhất, được Callbox lưu qua reboot. WCS phải phản hồi
về `callbox/001/cmd`; tất cả lệnh của giao dịch này dùng `ref_seq: 123`.

| Tình huống WCS | Payload command mẫu |
|---|---|
| Chấp nhận yêu cầu | `{"type":"accepted","task":1,"ref_seq":123,"ts":1786420801}` |
| Đã gán AGV | `{"type":"assigned","task":1,"ref_seq":123,"agv_id":"AGV-01","ts":1786420802}` |
| Khóa nhiệm vụ | `{"type":"locked","task":1,"ref_seq":123,"ts":1786420803}` |
| Hoàn tất | `{"type":"completed","task":1,"ref_seq":123,"ts":1786420810}` |
| Từ chối | `{"type":"rejected","task":1,"ref_seq":123,"reason":"duplicate","ts":1786420801}` |
| Quá hạn | `{"type":"overdue","task":1,"ref_seq":123,"ts":1786420815}` |

Giá trị `reason` được hỗ trợ khi từ chối: `locked`, `duplicate`, `no_task`,
`wcs_busy`.

### 4.2 Hủy nhiệm vụ

Khi người vận hành nhấn Hủy trong trạng thái cho phép, Callbox gửi:

```json
{"type":"cancel","task":1,"seq":124,"ts":1786420820}
```

WCS phản hồi:

```text
Topic: callbox/001/cmd
```

```json
{"type":"cancel_ack","task":1,"ref_seq":124,"ts":1786420821}
```

Nếu WCS không thể hủy do nhiệm vụ đã khóa, phản hồi `rejected` với `ref_seq`
bằng **seq của cancel** (không phải seq của call):

```json
{"type":"rejected","task":1,"ref_seq":124,"reason":"locked","ts":1786420821}
```

## 5. Quy tắc correlation bắt buộc

1. Không tự tạo `ref_seq` mới ở WCS.
2. `accepted`, `assigned`, `locked`, `completed`, `overdue` dùng `ref_seq`
   bằng `seq` của event `call` tương ứng.
3. `cancel_ack` hoặc `rejected` cho cancel dùng `ref_seq` bằng `seq` của event
   `cancel` tương ứng.
4. `sync` dùng `ref_seq` bằng `seq` của `sync_request` gần nhất.
5. Command sai topic, thiếu `task` (trừ `sync`), sai `ref_seq` hoặc payload
   JSON không hợp lệ sẽ bị Callbox bỏ qua để tránh áp dụng trạng thái cũ.

## 6. Status/heartbeat

Callbox publish retained status mỗi 1 giây:

```json
{
  "online": true,
  "comm": "ready",
  "task1": "assigned",
  "task2": "idle",
  "rssi": -41,
  "uptime": 150,
  "time_synced": true,
  "fw": "1.2.0",
  "ts": 1786420810
}
```

`comm` có ý nghĩa:

| Giá trị | Ý nghĩa |
|---|---|
| `ready` | MQTT kết nối và WCS đã xác nhận sync; Callbox sẵn sàng nhận nút. |
| `syncing` | MQTT đã kết nối, đang chờ WCS trả `sync`. |
| `offline` | Mất MQTT/WCS. |

## 7. Checklist nghiệm thu IT

- [ ] Broker chấp nhận kết nối TCP 1883 từ mạng của Callbox.
- [ ] ACL cho phép cả event, status và cmd đúng theo ID thiết bị.
- [ ] WCS nhận được `sync_request` của `001` và `002`.
- [ ] WCS phản hồi `sync` đúng topic và đúng `ref_seq`.
- [ ] Status chuyển từ `syncing` sang `ready`.
- [ ] Nhấn Task 1/2, WCS nhận được event `call` và phản hồi theo các bước.
- [ ] Nhấn Hủy, WCS trả `cancel_ack` hoặc `rejected` đúng `ref_seq`.
- [ ] Kiểm tra heartbeat retained mỗi 1 giây và LWT offline khi Callbox mất kết nối.
