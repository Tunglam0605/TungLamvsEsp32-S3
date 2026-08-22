# CallBox Flash/OTA - Phase 3: Generic OTA Service

## Status

Phase 3 implements the generic OTA transaction service above `platform_ota`.

The service is source-neutral and product-neutral. It does not know about CallBox missions, WCS/MQTT commands, WebUI routes, button gestures, tower lamps, buzzer patterns, network transport, or boot-health policy.

Dependency direction:

```text
future source/product adapters
          |
          v
     OTA service
          |
          v
     platform_ota
          |
          v
       ESP-IDF
```

No Phase 3 source calls `esp_ota_*` directly.

## Delivered component

```text
components/services/ota/
  CMakeLists.txt
  include/
    ota_types.h
    ota_events.h
    ota_service.h
  private_include/
    ota_service_private.h
  src/
    ota_service.c
    ota_session.c
    ota_validator.c
  test/
    CMakeLists.txt
    test_ota_service.c
```

The repository root now includes `components/services` in `EXTRA_COMPONENT_DIRS` so the service is built as an independent ESP-IDF component.

## Responsibilities

The generic OTA service owns one process-wide OTA transaction at a time. Its responsibilities are:

- serialize OTA transactions so two sources cannot write concurrently;
- admit a stream without erasing/writing flash until the image prefix is validated;
- accept arbitrarily small streaming chunks;
- support both known and unknown image lengths;
- track state, progress, target slot, metadata, last error, and staged-image status;
- select the inactive OTA slot through `platform_ota`;
- write only through `platform_ota`;
- finalize the image through `platform_ota_session_finish()`;
- expose product-neutral synchronous events;
- stage a valid image without activating it automatically;
- set the staged slot as boot target only when explicitly requested;
- never reboot by itself.

## State machine

```text
IDLE
  |
  | ota_service_begin()
  v
ADMISSION
  |
  | enough prefix bytes + metadata validation PASS
  | inactive target selected
  | platform session opened
  v
RECEIVING
  |
  | ota_service_finish()
  v
VERIFYING
  |
  | platform_ota_session_finish() PASS
  v
STAGED
  |
  | ota_service_install()
  v
INSTALLING
  |
  | platform_ota_set_boot_partition() PASS
  v
REBOOT_PENDING
```

Any admission, target-selection, write, size, or final validation failure transitions to:

```text
FAILED
```

`FAILED` never implies activation. `ota_service_discard()` clears `FAILED` or `STAGED` back to `IDLE`.

`ota_service_abort()` is available during an admitted/receiving transaction and returns the service to `IDLE` after cleaning the platform session.

## Admission before flash write

Phase 3 buffers only the initial application metadata prefix:

```text
esp_image_header_t          24 bytes
esp_image_segment_header_t   8 bytes
esp_app_desc_t             256 bytes
------------------------------------
bounded prefix             288 bytes
```

No full firmware image is buffered in RAM.

The first 288 bytes may arrive in any chunk sizes, including one byte at a time. Until this prefix is complete and validated, the service does not call `platform_ota_session_begin()` and therefore does not erase/write the inactive application slot.

Admission validates:

1. ESP image magic;
2. target chip ID against `CONFIG_IDF_FIRMWARE_CHIP_ID`;
3. `esp_app_desc_t` magic;
4. project name is valid and matches `esp_app_get_description()->project_name`;
5. version string is present and NUL-terminated;
6. known image size fits the selected OTA partition.

The service extracts:

- project name;
- version;
- secure version;
- final received image size.

## Validation split

Phase 3 deliberately splits validation into two layers.

### Admission validation

Runs before target flash writes and rejects clearly incompatible images early:

```text
image magic
chip ID
application descriptor
project identity
version metadata
known-size partition fit
```

### Final image validation

After all stream bytes are written, `VERIFYING` delegates to:

```text
ota_service
    -> platform_ota_session_finish()
        -> ESP-IDF esp_ota_end()
```

ESP-IDF therefore remains responsible for final image structural/checksum/hash/signature validation according to the enabled platform security configuration.

A download/upload completing is not considered OTA success. The image reaches `STAGED` only after finalization succeeds.

Phase 3 does not claim anti-rollback or post-boot health validation. Those are later phases.

## Streaming contract

### Known image size

`ota_service_begin(expected_size)` requires a non-zero expected size.

The service:

- rejects a chunk that would exceed the expected size;
- rejects a known image larger than the target partition;
- requires `bytes_received == expected_size` before finalization;
- moves to `FAILED` on truncation/overflow.

### Unknown image size

Use:

```c
OTA_IMAGE_SIZE_UNKNOWN
```

The service passes unknown-size semantics down to `platform_ota`. The platform layer still bounds writes to the physical target partition.

### Zero-length chunks

A zero-length write during `ADMISSION` or `RECEIVING` is a safe no-op. This simplifies generic streaming adapters that may occasionally produce empty buffers.

## Transaction ownership and concurrency

The service is a process-wide singleton protected by a FreeRTOS mutex.

Only one transaction can be outside `IDLE` at a time. A second `ota_service_begin()` is rejected with `ESP_ERR_INVALID_STATE`.

`ota_service_init()` is idempotent only while the service is `IDLE`. Calling it during an active/staged/install transaction is rejected so it cannot silently destroy an OTA handle.

`ota_service_reset()` may clean an active transaction, but it is rejected after `INSTALLING` or `REBOOT_PENDING`. Once the boot partition has been selected, generic reset cannot safely pretend that activation is no longer pending.

## Events

The generic service exposes:

```text
OTA_EVENT_STATE_CHANGED
OTA_EVENT_PROGRESS
OTA_EVENT_STAGED
OTA_EVENT_FAILED
OTA_EVENT_INSTALL_READY
OTA_EVENT_REBOOT_PENDING
```

Every event contains an immutable `ota_status_t` snapshot.

Callbacks are synchronous but are dispatched only after the OTA mutex is released. An adapter may safely call `ota_service_get_status()` from inside its callback without deadlocking the service.

The service does not map events to GPIO, tower lamps, buzzer, HTTP responses, MQTT topics, or business behavior. Those mappings belong to adapters/product policy in later phases.

## Failure semantics

For admission/write/finalization failures:

1. abort the platform session if it is still active;
2. preserve the primary `esp_err_t` in `last_error`;
3. clear `has_staged_image`;
4. transition to `FAILED`;
5. emit failure state/events;
6. never call `platform_ota_set_boot_partition()`.

An `esp_ota_end()` validation failure is already terminal for its ESP-IDF handle. Phase 2 closes the platform session after the end attempt; Phase 3 then records `FAILED` without attempting stale-handle reuse.

## Installation boundary

`ota_service_install()` is accepted only from `STAGED`.

It performs exactly:

```text
STAGED
  -> INSTALLING
  -> platform_ota_set_boot_partition(staged target)
  -> REBOOT_PENDING
```

It does not call `esp_restart()`.

This separation lets future CallBox policy decide when a safe reboot is allowed and lets Phase 4 own post-boot verification/rollback behavior.

## Public API

```c
ota_service_init()
ota_service_reset()

ota_service_begin()
ota_service_write()
ota_service_finish()
ota_service_abort()

ota_service_discard()
ota_service_install()

ota_service_get_status()
ota_service_set_event_callback()
```

The public component exports only its dependency on `platform_ota`. ESP-IDF image-format/app-update dependencies used by internal validation are private implementation dependencies.

## Unit-test seam

Tests replace platform operations and the running-app descriptor through the private `ota_service_ops_t` seam.

This allows deterministic fault injection without calling real `esp_ota_*` APIs or writing an OTA slot during service unit tests.

The test seam is private and must not be used by product/source adapters.

## Validation evidence

### Full CallBox production build

ESP-IDF v6.1-dev full production build passed with the new `ota` component linked independently.

```text
callbox_sews.bin: 0x120f00 bytes
smallest app partition: 0x200000 bytes
free in factory partition: 0xdf100 bytes (~44%)
```

### Phase 3 Unity HIL on COM15

A temporary Unity application was written only to the current factory application range starting at `0x10000`. The CallBox partition table, NVS partitions, OTA data, and A/B slots were not erased/replaced.

Eight Phase 3 test groups passed on the ESP32-S3:

1. tiny-chunk stream -> STAGED -> install -> REBOOT_PENDING without reboot;
2. invalid magic/chip/project/version/running-descriptor rejection before platform write;
3. lifecycle guards, zero-length write, init protection, reset cleanup;
4. unknown-size success and known-size truncation rejection;
5. write failure and final validation failure cleanup;
6. target-selection/session-begin provider failures never stage or activate;
7. callback reentrancy and abort-to-IDLE behavior;
8. REBOOT_PENDING reset protection and set-boot failure/discard recovery.

Result:

```text
8/8 test groups PASS
```

### Production restore regression

After the HIL harness, the production CallBox application was written back only at `0x10000`; flash hash verification passed.

Observed after restore:

```text
configuration loaded from migrated NVS
sequence loaded at 2133 and next reservation advanced to 2197
Wi-Fi AGV1 connected
DHCP address 192.168.1.107
MQTT connected and command subscription accepted
WCS mission sync accepted
Task 1 -> IDLE
Task 2 -> IDLE
Callbox ready
```

This confirms the Phase 3 test procedure did not erase persistent storage or change existing CallBox mission/network behavior.

## Explicitly deferred

### Phase 4 - Rollback and Boot Validation

- enable bootloader application rollback policy;
- handle `ESP_OTA_IMG_PENDING_VERIFY` at boot;
- boot-health qualification window;
- mark a new image valid;
- mark a failing image invalid and rollback;
- watchdog/panic/boot-loop rollback tests;
- power-loss and rollback HIL matrix.

Current production configuration intentionally still has bootloader application rollback disabled until Phase 4 implements the complete policy.

### Phase 5+ - CallBox adapters and OTA sources

- CallBox mission admission policy;
- WebUI upload route;
- AP/STA upload behavior;
- WCS/MQTT OTA trigger;
- button gesture trigger;
- LED/tower/buzzer rendering;
- reboot timing policy;
- remote HTTPS/server source adapter;
- source-specific retry/authentication UX.

## Phase 3 completion checklist

```text
generic OTA service component                 PASS
single transaction owner                     PASS
state machine                                PASS
bounded streaming                            PASS
prefix validation before flash write         PASS
known and unknown image sizes                PASS
final validation before STAGED               PASS
generic events/status                        PASS
failure cleanup                              PASS
explicit install without reboot              PASS
zero direct esp_ota_* calls in Phase 3       PASS
full production build                        PASS
ESP32-S3 Unity HIL                           PASS (8/8)
production firmware restored after HIL       PASS
CallBox Wi-Fi/MQTT/WCS regression            PASS
Phase 4+ product behavior excluded           PASS
```