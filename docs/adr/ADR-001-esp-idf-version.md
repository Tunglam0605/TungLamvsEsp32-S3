# ADR-001: ESP-IDF v6.1.0

## Decision

Use ESP-IDF **v6.1.0** with target `esp32s3`.

## Rationale

`callbox_sews/dependencies.lock` pins ESP-IDF 6.1.0 and ESP32-S3. Phase 1
uses the current `driver/i2c_master.h` API rather than the deprecated legacy
I2C driver used by part of the reference project.

## Consequence

Build environments and CI must invoke v6.1.0. An environment that only builds
from a previous cache is not accepted as verification.
