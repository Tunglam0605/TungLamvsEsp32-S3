# ADR-001: ESP-IDF v6.0.1

## Decision

Use the official stable ESP-IDF **v6.0.1** release with target `esp32s3`.
CI uses the exact `espressif/idf:v6.0.1` image tag; Windows verification must
use a checkout at the same release tag.

## Rationale

`callbox_sews/dependencies.lock` historically recorded ESP-IDF 6.1.0 and an
ESP32-S3 target, but its available Windows checkout identifies as `v6.1-dev`.
The `espressif/idf:v6.1.0` image tag is unavailable, so it cannot serve as
reproducible release evidence. Phase 0-1 therefore selects v6.0.1, an exact
available stable release, rather than treating the historical checkout or a
mutable branch image as a platform pin.

The BSP uses the current `driver/i2c_master.h` API rather than the legacy I2C
driver used by part of the reference project.

## Consequence

Build environments and CI must invoke v6.0.1 from a clean target/configuration
state. A previous v6.1-dev build or any cached Callbox artefact is not accepted
as verification of this platform.
