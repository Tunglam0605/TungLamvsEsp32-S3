# CallBox Flash/NVS OTA Foundation — Phase 0–1

This document records the implemented Phase 0–1 boundary. It does **not** enable OTA transport, WebUI upload, image activation, rollback policy, button gestures, WCS OTA commands, or OTA output patterns.

## Phase 0 — Frozen flash map

`partitions.csv` targets the 16 MiB ESP32-S3:

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| `nvs` | `0x9000` | 24 KiB | legacy migration source |
| `phy_init` | `0xF000` | 4 KiB | PHY init |
| `factory` | `0x10000` | 2 MiB | recovery/factory application |
| `otadata` | `0x210000` | 8 KiB | ESP-IDF A/B boot metadata |
| `nvs_cfg` | `0x214000` | 128 KiB | low-write configuration |
| `nvs_runtime` | `0x234000` | 128 KiB | runtime/high-write state |
| `coredump` | `0x254000` | 256 KiB | diagnostics |
| `ota_0` | `0x2A0000` | 4 MiB | future OTA slot A |
| `ota_1` | `0x6A0000` | 4 MiB | future OTA slot B |
| `storage` | `0xAA0000` | 2 MiB | reserved general storage |
| `reserve` | `0xCA0000` | 3456 KiB | future expansion |

The layout ends at `0x1000000` and both OTA slots are 64 KiB aligned. ESP-IDF generated the partition binary successfully and reported the active CallBox image (`0x120f00` bytes during Phase 0–1 validation) fits the 2 MiB factory slot with about 44% free.

The partition-table conversion is a maintenance/provisioning operation, not a normal OTA update. Legacy `nvs` at `0x9000` is retained as migration input.

## Phase 1 — Storage foundation

### Ownership

- `platform_nvs` owns ESP-IDF NVS mechanics and now supports named-partition init/open.
- `callbox_config_store` owns product configuration and reads/writes `nvs_cfg/callbox`.
- `sequence_store` owns sequence persistence and reads/writes `nvs_runtime/sequence.high_watermark`.
- `callbox_storage_migration` owns the one-way legacy migration policy.

### Boot data flow

```text
legacy nvs init (never auto-erase)
        |
        v
init nvs_cfg + nvs_runtime
        |
        v
write schema marker = IN_PROGRESS
        |
        +--> copy CallBox/Wi-Fi/MQTT/SNTP/Web config -> nvs_cfg/callbox
        |
        +--> copy sequence high-watermark -> nvs_runtime/sequence
        |       target := max(legacy, existing runtime)
        |
        v
commit + read-back verify all copied data
        |
        v
write + read-back verify schema marker = VERIFIED/version 1
        |
        v
load config + initialize sequence service
```

The migration is idempotent. An interrupted migration retries. A verified migration becomes a no-op. A future schema version greater than the supported version is rejected. Legacy NVS is never erased automatically.

### Migrated data

Configuration migration covers Wi-Fi credentials/static network values, Wi-Fi profile list, MQTT broker/port/transport/credentials, CallBox ID, Web password, and SNTP servers. `mqtt_port` is preserved as `u16`.

Sequence migration preserves monotonicity. If a retry occurs after `nvs_runtime` has already advanced, migration keeps `max(legacy_high_watermark, runtime_high_watermark)` so sequence numbers cannot move backwards or be reused.

## Hardware validation — COM15

Validated on the connected ESP32-S3 without chip erase:

- partition table generated and flashed successfully;
- firmware and bootloader hashes verified by esptool;
- legacy NVS remained outside all erased flash ranges;
- first hardware run exposed and fixed an NVS key-length issue in the migration marker;
- corrected firmware booted normally;
- legacy Wi-Fi configuration was preserved (`AGV1`);
- device received `192.168.1.107`;
- MQTT reconnected to `wcs.aubot.vn:1883`;
- WCS mission sync completed;
- sequence continued to `1685`, confirming continuity rather than reset to zero.

## Explicitly deferred

Phase 2+ remains intentionally absent from this implementation: `platform_ota` write/activation primitives, streaming OTA service, rollback validator, WCS/WebUI OTA triggers, button gestures, and OTA-specific LED/buzzer policy.
