# HIL result template

Copy this file to `HIL-YYYYMMDD-board-<identifier>.md`. Do not mark a field as
passed without the corresponding log, measurement, or physical observation.

## Test identity

- Date/time and timezone:
- Operator:
- Board label/revision:
- Module marking:
- Firmware commit SHA:
- ESP-IDF version:
- Power source:
- Attached load/test fixture:

## Build, flash, and boot

- Clean-build result:
- Flash result:
- Chip/revision:
- Reset reason:
- Flash detected:
- PSRAM detected:
- Partition table:
- Panic/watchdog/reset loop:

## BSP, TCA9554, and safe state

- BSP initialization:
- I2C initialization:
- TCA9554 address `0x20` communication:
- Direction register configured as output:
- `desired_mask`:
- `applied_mask`:
- `applied_valid`:
- `safe_mask`:
- Safe-state write result:
- Reset/power-cycle transient evidence: not verified / visual / multimeter /
  oscilloscope

## Digital inputs

| Channel | Raw inactive | Raw active | Logical active | Result |
|---|---:|---:|---:|---|
| DI1 | | | | NOT TESTED |
| DI2 | | | | NOT TESTED |
| DI3 | | | | NOT TESTED |
| DI4 | | | | NOT TESTED |
| DI5 | | | | NOT TESTED |
| DI6 | | | | NOT TESTED |
| DI7 | | | | NOT TESTED |
| DI8 | | | | NOT TESTED |

- Provisional polarity retained or resolved:
- BOOT button observation:

## Indicators

- RGB red/green/blue/off physical observation:
- RGB order/flicker:
- Buzzer on/off physical observation:
- Buzzer frequency/behavior:

## Digital outputs

Stage 1 leaves the output sequence disabled. Complete this table only in a
separately approved test with isolated loads or a safe fixture.

| Logical channel | Physical output | OFF measured | ON measured | Mapping |
|---|---|---|---|---|
| DO1 | EXIO1 | NOT TESTED | NOT TESTED | NOT VERIFIED |
| DO2 | EXIO2 | NOT TESTED | NOT TESTED | NOT VERIFIED |
| DO3 | EXIO3 | NOT TESTED | NOT TESTED | NOT VERIFIED |
| DO4 | EXIO4 | NOT TESTED | NOT TESTED | NOT VERIFIED |
| DO5 | EXIO5 | NOT TESTED | NOT TESTED | NOT VERIFIED |
| DO6 | EXIO6 | NOT TESTED | NOT TESTED | NOT VERIFIED |
| DO7 | EXIO7 | NOT TESTED | NOT TESTED | NOT VERIFIED |
| DO8 | EXIO8 | NOT TESTED | NOT TESTED | NOT VERIFIED |

## Raw boot log excerpt

Include firmware/IDF version, chip revision, reset reason, Flash/PSRAM,
partition table, BSP/I2C/TCA9554/safe-state, DO masks, DI masks, BOOT, RGB,
buzzer, and every warning/error. Remove unnecessary personal machine paths.

```text
PENDING OPERATOR LOG
```

## Open issues and decision

- GPIO21 remains `DOCUMENT_CONFLICT` unless exact-revision evidence resolves it.
- Unresolved items:
- Final decision: PASS / FAIL / PARTIAL / BLOCKED
- Pin-map labels changed (must link to this result): none
