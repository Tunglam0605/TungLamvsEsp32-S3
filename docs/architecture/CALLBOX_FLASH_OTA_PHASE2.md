# CallBox Flash/OTA - Phase 2: Platform OTA

## Status

Phase 2 is implemented and hardware-validated on the ESP32-S3 CallBox board.

This phase implements only the platform-facing OTA primitives required by the architecture specification. It does **not** implement the generic OTA service, transport/source adapters, CallBox OTA policy, WebUI upload, WCS commands, button triggers, output patterns, or post-boot health policy.

## Scope delivered

Component:

```text
components/platform/platform_ota/
  CMakeLists.txt
  include/platform_ota.h
  private_include/platform_ota_internal.h
  platform_ota.c
  platform_ota_idf.c
  test/test_platform_ota.c
```

Responsibilities:

- discover the running application partition;
- select the ESP-IDF next OTA update partition and reject any running-partition conflict;
- open one OTA write session;
- stream chunks directly to ESP-IDF without full-image RAM buffering;
- enforce target-partition and expected-image-size bounds before writes;
- finish and validate through `esp_ota_end()`;
- abort an in-progress write;
- select a completed inactive OTA partition as the next boot partition;
- query ESP-IDF OTA image state;
- expose mark-valid and mark-invalid/rollback/reboot primitives for the later boot-validation phase.

## Dependency boundary

```text
future OTA service / product policy
              |
              v
         platform_ota
         |           |
         |           +-- lifecycle, bounds, target safety
         |
         +-- platform_ota_idf.c
                    |
                    v
                 ESP-IDF
```

`platform_ota_idf.c` is the production backend that calls `esp_ota_*`. Higher layers must not call `esp_ota_*` directly.

The private ops table exists only as a test seam. Product code must use the public `platform_ota.h` API.

## Session contract

A caller must initialize a session before use:

```text
platform_ota_session_init
        |
        v
platform_ota_session_begin
        |
        +--> platform_ota_session_write (0..N chunks)
        |
        +--> platform_ota_session_finish
        |
        `--> platform_ota_session_abort
```

Key rules:

1. A running partition can never be accepted as an OTA target.
2. A known image size must fit the selected OTA partition.
3. Chunk writes cannot exceed the target partition or the declared image size.
4. Zero-length writes are safe no-ops.
5. Unknown-length streaming maps to ESP-IDF `OTA_WITH_SEQUENTIAL_WRITES`.
6. `finish` refuses a known-length image until exactly the declared byte count has been written.
7. `esp_ota_end()` consumes the ESP-IDF OTA handle even when validation fails; therefore the platform session is always closed after the backend `end` call.
8. Abort also closes the platform session even if the backend reports an error, preventing stale-handle reuse.
9. Activation is a separate operation and is not performed by `finish`.

## Rollback boundary

Phase 2 exposes these low-level primitives:

- `platform_ota_get_partition_state()`
- `platform_ota_mark_running_valid()`
- `platform_ota_mark_running_invalid_and_rollback_reboot()`

The current production sdkconfig intentionally still has bootloader application rollback disabled. Enabling `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, handling `PENDING_VERIFY`, health qualification, and boot-loop/rollback testing belong to **Phase 4 - Rollback & Boot Validation**. Phase 2 only provides the platform abstraction needed by that phase.

## Validation

### Production build

ESP-IDF v6.1-dev full CallBox build passed with `platform_ota` linked as an independent component.

Result:

```text
callbox_sews.bin: 0x120f00 bytes
factory partition: 0x200000 bytes
free in factory partition: about 44%
```

No existing CallBox business component was changed to depend on OTA in Phase 2.

### Hardware unit test - COM15

A temporary ESP-IDF Unity application was app-flashed only to the existing factory application region. The production partition table, NVS partitions, and OTA data were not erased or replaced.

The following 10 Phase 2 wrapper tests passed on the ESP32-S3:

1. selects inactive OTA partition;
2. rejects running partition as update target;
3. streams a known-size image and finishes;
4. treats zero-length write as backend no-op;
5. prevents writes past expected size;
6. closes the platform session after `esp_ota_end()` failure;
7. closes the platform session after abort failure;
8. maps unknown image size to sequential-write mode;
9. delegates activation and rollback primitives;
10. maps ESP-IDF image state.

The tests use a fake backend to validate lifecycle and error semantics deterministically while executing the wrapper code on the real ESP32-S3 target.

### Restore and regression check

After HIL testing, the production CallBox application was app-flashed back to the factory application address only. Hash verification passed.

Observed after restore:

```text
configuration loaded from migrated NVS
sequence high watermark preserved and continued increasing
Wi-Fi connected
MQTT connected
WCS sync accepted
Task 1 -> IDLE
Task 2 -> IDLE
Callbox ready
```

This confirms the Phase 2 work did not alter or erase persistent configuration/runtime storage and did not change existing CallBox mission/network behavior.

## Explicitly deferred

### Phase 3

- generic OTA service/state machine;
- ownership of complete OTA transactions;
- HTTP(S) streaming/download integration;
- source-independent session API;
- retry/progress/failure policy.

### Phase 4

- enable ESP-IDF bootloader rollback configuration;
- `PENDING_VERIFY` handling;
- boot health qualification;
- mark-valid policy;
- mark-invalid and automatic rollback integration;
- boot-loop and power-loss rollback HIL tests.

### Phase 5+

- CallBox mission policy;
- WebUI OTA;
- WCS OTA trigger;
- button gesture trigger;
- OTA LED/buzzer rendering;
- server-side OTA source adapter.

## Phase 2 completion criteria

Phase 2 is complete when all of the following are true:

```text
platform_ota component exists                     PASS
unit-testable ESP-IDF wrapper                     PASS
A/B discovery/write/activation primitives         PASS
rollback/image-state primitives                   PASS
running partition write protection                PASS
bounded streaming without full-image RAM buffer   PASS
production CallBox build                          PASS
ESP32-S3 hardware wrapper tests                    PASS (10/10)
production firmware restored after HIL             PASS
CallBox normal boot/Wi-Fi/MQTT/WCS regression     PASS
no Phase 3+ product behavior added                 PASS
```