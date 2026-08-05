# TungLam ESP32-S3 Platform Firmware

Reusable ESP-IDF platform firmware for the Waveshare
ESP32-S3-POE-ETH-8DI-8DO. This repository contains a clean board-support
foundation, not Callbox, AGV, or WCS business logic.

## Current scope: v0.1.0 foundation and BSP core

- ESP-IDF project pinned to **v6.1.0** and target `esp32s3`.
- Default expected profile: ESP32-S3-WROOM-1U-N16R8 (16 MB Flash / 8 MB
  PSRAM). Runtime diagnostics warn when detected hardware differs.
- TCA9554 low-level driver and Waveshare BSP for DI, DO, I2C, buzzer, RGB,
  BOOT button, safe state, and a single `board_hardware_test` firmware.
- No Wi-Fi, Ethernet, BLE, MQTT, HTTP, OTA, provisioning, Callbox, WCS, AGV,
  RS485, CAN, RTC, or TF card implementation in this phase.

See [hardware documentation](docs/hardware/pin-map.md), including the open
GPIO21 RS485/SD conflict, before enabling unimplemented peripherals.

## Windows setup and clean build

Use an ESP-IDF v6.1.0 environment. In Espressif PowerShell, or after running
the v6.1.0 `export.ps1` script:

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

- Build: Windows clean build passed with the installed ESP-IDF `v6.1-dev`
  checkout; exact `v6.1.0` release parity remains pending.
- Unit tests: host test is present but not executable on this machine because
  no native host C toolchain/SDK is installed.
- Hardware test: not verified on physical hardware.
- CI: workflow is pinned to ESP-IDF `v6.1.0`; its two remote runs failed
  before the build because that exact Docker image tag has no manifest.

See the detailed [verification status](docs/testing/verification-status.md)
and [Callbox reference assessment](docs/reference/callbox-sews-assessment.md).
