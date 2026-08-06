# HIL result — Stage 1 partial validation

## Test identity

- Date/time and timezone: 2026-08-06 10:24–10:25 UTC+07:00.
- Operator: user; personal identity not recorded.
- Board label/revision: unidentified; the connected device was confirmed by the
  operator as the target ESP32 on COM15.
- Module marking: not recorded.
- Firmware commit SHA: `e4dcff4552a25d1cf8162fc03d04550b796b8ba0`.
- ESP-IDF version: v6.0.1.
- Power source: not recorded.
- Attached load/test fixture: not recorded. The DO sequence was disabled, so
  no DO1–DO8 activation was requested.

## Build, flash, and boot

- Clean-build result: `set-target`, `fullclean`, and `build` passed with the
  same source content before the firmware commit was created; reconfigure and
  build of the committed image passed immediately before flashing.
- Flash result: passed on COM15. esptool verified the written hashes for the
  bootloader, partition table, and application image.
- Chip/revision: ESP32-S3 (QFN56), revision v0.2.
- Reset reason: 11 (`USB_UART_CHIP_RESET`) after the controlled RTS reset used
  to capture the boot log.
- Flash detected: 16 MiB; DIO, 80 MHz.
- PSRAM detected: 8 MiB; octal PSRAM memory test passed.
- Partition table: `nvs` 0x9000/0x6000, `phy_init` 0xf000/0x1000, and factory
  app 0x10000/0xff0000.
- Panic/watchdog/reset loop: none observed during the 10-second monitor
  capture. This is not a power-cycle test.

## BSP, TCA9554, and safe state

- BSP initialization: passed.
- I2C initialization: passed on I2C0, SDA GPIO42, SCL GPIO41.
- TCA9554 address `0x20` communication: initialization passed.
- Direction register configured as output: the BSP initialization completed
  after its output-direction write; no register readback was performed.
- `desired_mask`: `0x00`.
- `applied_mask`: `0x00`.
- `applied_valid`: `true`.
- `safe_mask`: `0x00`.
- Safe-state write result: logical safe state applied at initialization and
  again on test cleanup. Physical OFF voltage/current was not measured.
- Reset/power-cycle transient evidence: one USB/UART reset was monitored;
  power-cycle and transient measurements are not verified.

## Digital inputs

Initial monitor state was raw `0xff`, logical `0x00`, using the provisional
raw-LOW-is-active mapping. No input was electrically actuated.

| Channel | Raw inactive | Raw active | Logical active | Result |
|---|---:|---:|---:|---|
| DI1 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |
| DI2 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |
| DI3 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |
| DI4 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |
| DI5 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |
| DI6 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |
| DI7 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |
| DI8 | NOT MEASURED | NOT MEASURED | NOT VERIFIED | NOT TESTED |

- Provisional polarity retained or resolved: retained.
- BOOT button observation: one released sample was logged; press/release was
  not tested, so active-low interpretation remains provisional.

## Indicators

- RGB red/green/blue/off physical observation: command sequence completed;
  no visual observation was recorded.
- RGB order/flicker: not verified.
- Buzzer on/off physical observation: the operator heard one beep during the
  100 ms buzzer test.
- Buzzer frequency/behavior: BSP is configured for 1 kHz PWM at 50% duty;
  the frequency and electrical waveform were not measured.

## Digital outputs

`CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE` was disabled for this
run. No DO output was commanded ON.

| Logical channel | Physical output | OFF measured | ON measured | Mapping |
|---|---|---|---|---|
| DO1 | EXIO1 | NOT MEASURED | NOT TESTED | NOT VERIFIED |
| DO2 | EXIO2 | NOT MEASURED | NOT TESTED | NOT VERIFIED |
| DO3 | EXIO3 | NOT MEASURED | NOT TESTED | NOT VERIFIED |
| DO4 | EXIO4 | NOT MEASURED | NOT TESTED | NOT VERIFIED |
| DO5 | EXIO5 | NOT MEASURED | NOT TESTED | NOT VERIFIED |
| DO6 | EXIO6 | NOT MEASURED | NOT TESTED | NOT VERIFIED |
| DO7 | EXIO7 | NOT MEASURED | NOT TESTED | NOT VERIFIED |
| DO8 | EXIO8 | NOT MEASURED | NOT TESTED | NOT VERIFIED |

## Raw boot log excerpt

The complete cleaned UART capture is retained outside Git. This excerpt
contains all application diagnostics and no warnings or errors.

```text
I (24) boot: ESP-IDF v6.0.1 2nd stage bootloader
I (39) boot.esp32s3: SPI Flash Size : 16MB
I (200) esp_psram: Found 8MB PSRAM device
I (642) esp_psram: SPI SRAM memory test OK
I (662) app_init: App version:      e4dcff4
I (675) app_init: ESP-IDF:          v6.0.1
I (754) platform_boot: Starting board_hardware_test firmware
I (774) board_hw_test: Firmware: project=tunglam_esp32_s3_platform version=e4dcff4 idf=v6.0.1
I (784) board_hw_test: Reset reason: 11
I (794) bsp_i2c: I2C initialized: port=0 SDA=GPIO42 SCL=GPIO41
I (804) bsp_do: TCA9554 initialized at I2C address 0x20
I (804) bsp_do: Safe logical DO mask applied: 0x00 (provisional register polarity: active-high)
I (824) board_hw_test: Detected Flash: 16 MiB
I (834) board_hw_test: Detected PSRAM: 8 MiB
I (834) board_hw_test: DI raw=0xff logical=0x00 (provisional raw-LOW-is-active)
I (844) board_hw_test: BOOT button currently released (provisional active-low interpretation)
I (854) board_hw_test: DO state after BSP initialization: desired=0x00 applied=0x00 valid=1 safe=0x00
I (1314) board_hw_test: RGB test command completed; visual confirmation remains HIL evidence
I (1414) board_hw_test: Buzzer test command completed; audible confirmation remains HIL evidence
I (1414) board_hw_test: DO sequence disabled by CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE
I (1414) board_hw_test: DO state after safe-state restore: desired=0x00 applied=0x00 valid=1 safe=0x00
```

## Open issues and decision

- GPIO21 remains `DOCUMENT_CONFLICT`.
- DI polarity/mapping, BOOT press polarity, RGB observation, physical DO
  polarity/mapping, and reset/power-cycle transients remain unresolved.
- Final decision: PARTIAL. Stage 1 software communication and logical safe
  state passed; this record does not establish physical DI/DO/RGB behavior.
- Pin-map labels changed: none.
