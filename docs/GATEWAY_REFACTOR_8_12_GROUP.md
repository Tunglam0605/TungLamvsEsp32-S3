# Gateway 8/12 Group refactor

Production domain hierarchy:

`BSP CAN -> Laser protocol/profile -> Warehouse group snapshot -> WebUI/MQTT serializer`

- `GROUP_8` has 8 warehouse positions; `GROUP_12` has 12.
- Each position owns at most one representative LaserID in its table-defined range.
- Runtime starts `UNKNOWN`; only a valid online status produces `EMPTY` or `OCCUPIED`.
- NVS namespace `warehouse_v3` stores configuration only. Legacy 64-slot keys are not silently remapped.
- MQTT `TRANSITIONAL_GROUP_SNAPSHOT_V1` is not a frozen WCS contract.
- Profile-12 obstacle-event CAN IDs are TBD. Old 100/110 bases are ambiguous above group 10, so profile 12 uses status byte 7 (`Warn`) only.
- Tower RED/YELLOW/GREEN DO mapping is TBD; no output channel is guessed or energized.

Manual verification matrix:

| Check | Expected |
|---|---|
| GROUP_8 ID 46 / 47 | G5 / G6 |
| GROUP_12 ID 43 / 44 / 62 | G5 / G6 / G12 |
| Duplicate LaserID | HTTP 409 `DUPLICATE_LASER_ID` |
| Duplicate warehouse code | HTTP 409 `DUPLICATE_WAREHOUSE_CODE` |
| Distance_E > Distance | HTTP 409 `INVALID_DISTANCE` |
| Warn 0 / 1 / 2 | EMPTY / OCCUPIED / OCCUPIED |
| Sensor timeout/offline | UNKNOWN |
| ETH debug has IP | production network remains false |
| ETH uplink has IP | production network true |
| MQTT reconnect | immediate current snapshot requested |
