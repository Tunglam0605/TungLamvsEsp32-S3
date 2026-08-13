# Gateway 8/12 Group refactor

Production domain hierarchy:

`BSP CAN -> Laser protocol/profile -> Warehouse group snapshot -> WebUI/MQTT serializer`

- Firmware Gateway hiện cố định `GROUP_12` với 12 vị trí kho.
- Each position owns at most one representative LaserID in its table-defined range.
- Runtime starts `UNKNOWN`; only a valid online status produces `EMPTY` or `OCCUPIED`.
- NVS namespace `warehouse_v3` stores configuration only. Legacy 64-slot keys are not silently remapped.
- Profile 8 obstacle events use Emergency `100 + group_index` and Normal `110 + group_index`.
- Profile 12 obstacle events use Emergency `90 + group_index` and Normal `105 + group_index`.
- Status byte 7 (`Warn`) remains the authoritative current-state input. Group CAN events are auxiliary fast events and diagnostic counters only.
- Tower RED/YELLOW/GREEN DO mapping is TBD; no output channel is guessed or energized.

## MQTT contract: WAREHOUSE_STATUS_V1

Contract tích hợp chính thức nằm tại
[GATEWAY_MQTT_CONTRACT_V1.md](GATEWAY_MQTT_CONTRACT_V1.md). Tóm tắt namespace:

```text
warehouse/sensor/{company_id}/{site_id}/{warehouse_id}/status/json
warehouse/sensor/{company_id}/{site_id}/{warehouse_id}/status/bits
```

- Hai topic status dùng QoS 1, retained và được tạo từ cùng snapshot.
- Snapshot luôn giữ đủ 12 vị trí. `EMPTY=00`, `OCCUPIED=01`,
  `UNKNOWN=10`, `FAULT=11`.
- Một Gateway sở hữu một kho logic. `warehouse_id` được sinh tự động bằng cách
  chuyển `gateway_id` sang chữ thường; `warehouse_name` bằng `gateway_id`.
- WebUI không cấu hình thêm mã kho hoặc tên kho độc lập.
- Chỉ `status/json` và `status/bits` thuộc contract MQTT ngoài; không có
  `event`, `cmd` hoặc `availability` riêng.
- A full retained snapshot is sent immediately after connect/reconnect, after a current-state/config change, and periodically.
- TCP MQTT does not wait for SNTP. TLS waits for valid time before certificate validation.
- No state history is replayed by the Gateway; WCS owns history.

Manual verification matrix:

| Check | Expected |
|---|---|
| GROUP_8 ID 46 / 47 | G5 / G6 |
| GROUP_12 ID 43 / 44 / 62 | G5 / G6 / G12 |
| GROUP_8 event boundary | Emergency 100..107 / Normal 110..117 |
| GROUP_12 event boundary | Emergency 90..101 / Normal 105..116 |
| Duplicate LaserID | HTTP 409 `DUPLICATE_LASER_ID` |
| Duplicate warehouse code | HTTP 409 `DUPLICATE_WAREHOUSE_CODE` |
| Distance_E > Distance | HTTP 409 `INVALID_DISTANCE` |
| Warn 0 / 1 / 2 | EMPTY / OCCUPIED / OCCUPIED |
| Sensor timeout/offline ở vị trí đã cấu hình | FAULT |
| ETH debug has IP | production network remains false |
| ETH uplink has IP | production network true |
| MQTT reconnect | immediate current snapshot requested |

## Địa chỉ truy cập cục bộ

- AP cấu hình: `192.168.65.204/24`.
- Ethernet chế độ debug máy tính: `192.168.66.204/24`.
- WiFi STA và Ethernet uplink: dùng IP runtime do DHCP cấp hoặc IP tĩnh đã cấu
  hình. WebUI hiển thị riêng IP của AP, STA và Ethernet.

AP và Ethernet debug dùng hai subnet khác nhau để tránh route cục bộ bị nhập
nhằng khi cả hai interface cùng hoạt động.
