# TungLam ESP32-S3 Platform Firmware

Reusable ESP-IDF platform firmware for the Waveshare
ESP32-S3-POE-ETH-8DI-8DO. This repository contains a clean board-support
foundation, not Callbox, AGV, or WCS business logic.

## Current scope: v0.1.0 foundation and BSP core

- ESP-IDF project pinned to official stable **v6.0.1** and target `esp32s3`.
- Default expected profile: ESP32-S3-WROOM-1U-N16R8 (16 MB Flash / 8 MB
  PSRAM). Runtime diagnostics warn when detected hardware differs.
- TCA9554 low-level driver and Waveshare BSP for DI, DO, I2C, buzzer, RGB,
  BOOT button, safe state, and a single `board_hardware_test` firmware.
- No Wi-Fi, Ethernet, BLE, MQTT, HTTP, OTA, provisioning, Callbox, WCS, AGV,
  RS485, CAN, RTC, or TF card implementation in this phase.

See [hardware documentation](docs/hardware/pin-map.md), including the open
GPIO21 RS485/SD conflict, before enabling unimplemented peripherals.

## Windows setup and clean build

Use an ESP-IDF v6.0.1 environment. In Espressif PowerShell, or after running
the v6.0.1 `export.ps1` script:

```powershell
idf.py --version
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Flash and monitor a connected board only after choosing its COM port:

```powershell
idf.py -p COMx flash
idf.py -p COMx monitor
```

The full Windows environment and verification record is maintained in
[docs/testing/windows-clean-build.md](docs/testing/windows-clean-build.md).

## Layers

```text
board_hardware_test -> Waveshare BSP -> drv_tca9554 -> ESP-IDF / FreeRTOS
```

`main` is deliberately only a bootstrap. The application uses public BSP APIs;
it never owns GPIO, I2C, or TCA9554 directly.

## Creating a product project

Depend on a stable platform version/tag and place product-specific behavior in
that product's application layer. Do not add Callbox, WCS, AGV, credentials,
or product MQTT topics to this repository.

## Verification status

- Build: exact ESP-IDF `v6.0.1` clean-build verification is recorded in the
  test status; the earlier `v6.1-dev` compilation is historical only.
- Unit tests: host test covers DO state transitions and passed independently
  in GitHub Actions run `31063665960`.
- Hardware test: not verified on physical hardware.
- CI: GitHub Actions run `31063665960` passed the ESP-IDF v6.0.1 clean build
  and native host-test jobs.

See the detailed [verification status](docs/testing/verification-status.md)
and [Callbox reference assessment](docs/reference/callbox-sews-assessment.md).
