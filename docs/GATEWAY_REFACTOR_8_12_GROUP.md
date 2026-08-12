# Gateway 8/12 Group refactor

Production domain hierarchy:

`BSP CAN -> Laser protocol/profile -> Warehouse group snapshot -> WebUI/MQTT serializer`

- `GROUP_8` has 8 warehouse positions; `GROUP_12` has 12.
- Each position owns at most one representative LaserID in its table-defined range.
- Runtime starts `UNKNOWN`; only a valid online status produces `EMPTY` or `OCCUPIED`.
- NVS namespace `warehouse_v3` stores configuration only. Legacy 64-slot keys are not silently remapped.
- Profile 8 obstacle events use Emergency `100 + group_index` and Normal `110 + group_index`.
- Profile 12 obstacle events use Emergency `90 + group_index` and Normal `105 + group_index`.
- Status byte 7 (`Warn`) remains the authoritative current-state input. Group CAN events are auxiliary fast events and diagnostic counters only.
- Tower RED/YELLOW/GREEN DO mapping is TBD; no output channel is guessed or energized.

## MQTT contract: JSON_WAREHOUSE_V1

All topics use the logical `gateway_id` stored in NVS:

| Topic | QoS | Retain | Direction |
|---|---:|---:|---|
| `gateway/{gateway_id}/availability` | 1 | yes | Gateway → WCS; also Last Will |
| `gateway/{gateway_id}/status` | 1 | yes | Gateway → WCS full current snapshot |
| `gateway/{gateway_id}/event` | 1 | no | Gateway → WCS state transition / pong |
| `gateway/{gateway_id}/cmd` | 1 | no | WCS → Gateway |

- `status` contains only enabled positions. `summary.total` is the number of enabled positions and always equals `empty + occupied + unknown`.
- `boot_id` is regenerated at boot. `seq` increases monotonically during that boot and resets with the next `boot_id`.
- A full retained snapshot is sent immediately after connect/reconnect, after a current-state/config change, on `request_snapshot`, and periodically.
- `cmd` v1 accepts only `request_snapshot` and `ping`. It cannot set warehouse state or configure a Laser.
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
| Sensor timeout/offline | UNKNOWN |
| ETH debug has IP | production network remains false |
| ETH uplink has IP | production network true |
| MQTT reconnect | immediate current snapshot requested |
| MQTT `request_snapshot` | immediate current snapshot requested |
