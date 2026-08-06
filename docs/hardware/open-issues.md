# Open hardware issues

## GPIO21: RS485 RTS versus SD card CS

| Source | GPIO21 meaning | Status |
|---|---|---|
| Waveshare Wiki | RS485 RTS | VERIFIED_VENDOR_DOC |
| Waveshare Arduino demo (`WS_GPIO.h`) | `TXD1EN` / RS485 direction | VERIFIED_VENDOR_DEMO |
| Supplied reference pin table | SD_CS | DOCUMENT_CONFLICT |

No schematic for the exact board revision was available in the official Wiki or
the downloaded official demo package. Phase 0-1 does not initialize TF-card or
RS485 code. Do not claim that RS485 and TF-card can operate simultaneously
until an exact-revision schematic or physical HIL test resolves this conflict.

## Digital-input logical active state

DI GPIO4..GPIO11 mapping is vendor-verified, but their logical active polarity
is not. `PLATFORM_DI_ACTIVE_LOW_PROVISIONAL` is explicit in Kconfig and the
hardware-test application prints both raw electrical and translated logical
input masks. HIL must establish the final default.

## N16R8 profile

Waveshare documents ESP32-S3-WROOM-1U-N16R8 as the default module but notes
that other module variants are customizable. Runtime diagnostics compare the
detected Flash/PSRAM sizes with the expected 16 MiB / 8 MiB profile; a mismatch
is a warning requiring board identification, not an automatic assumption.

The clean-build profile emits a DIO/16 MB Flash image and enables octal PSRAM.
Those are build settings, not an HIL result: boot/Flash/PSRAM detection on the
actual board must be captured before treating the current profile as verified.
