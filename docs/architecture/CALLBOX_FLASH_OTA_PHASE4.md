# CallBox Flash/OTA - Phase 4: Rollback and Boot Validation

## Scope

Phase 4 enables `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` in the normal and
production defaults. It deliberately does **not** enable anti-rollback eFuse
or secure-version policy.

The dependency path is kept as:

```text
CallBox boot qualifier -> boot_validation service -> platform_ota -> ESP-IDF esp_ota_*
```

`platform_ota` remains the only component containing direct `esp_ota_*` calls.
The generic `boot_validation` service discovers the running partition state,
reports whether this boot is `PENDING_VERIFY`, and delegates mark-valid and
mark-invalid/rollback requests to that platform layer. It has no knowledge of
Wi-Fi, MQTT, WebUI, CallBox tasks, or health policy.

## Timing and qualification

At the beginning of `callbox_app_run`, the generic service reads the running
partition state. A non-pending boot does nothing.

For a pending normal image, CallBox starts qualification only after all normal
startup calls have succeeded: NVS initialization and migration, config and
sequence initialization, board initialization, Wi-Fi subsystem start, MQTT
initialization, business task creation, and health-monitor initialization.
The product qualifier then observes fifteen seconds of local health-monitor
check-in counters. Each required local worker must advance at least once:

- I/O handler, state machine, MQTT supervisor and MQTT TX;
- output renderer, network-status task, and Wi-Fi selection task.

Broker connection, DHCP completion, and external WCS reachability are not
qualification gates. MQTT must initialize and its local retry/supervisor tasks
must run, so loss of external infrastructure cannot permanently prevent a good
image from becoming valid.

Recovery mode qualifies after its reduced NVS/config/AP/WebUI/health-monitor
startup path and the same fifteen-second bounded observation. It intentionally
does not require normal business, STA, or MQTT workers.

There are no health-monitor callbacks in this path. The qualifier is a low
priority task that takes two short, lock-protected snapshots; it never runs
under an OTA or health-monitor lock.

## Failure semantics

ESP-IDF rollback flow is:

```text
activated image -> NEW -> first boot PENDING_VERIFY
  qualified -> mark_app_valid_cancel_rollback -> VALID
  reset/panic/WDT before qualification -> bootloader rollback on next boot
  controlled local startup/qualification failure -> mark invalid + rollback reboot
```

Every existing controlled startup failure first requests rollback if the
running image is pending. The ESP-IDF invalid/rollback API normally reboots.
If that API returns an error, the established health-monitor controlled restart
remains the fallback; it preserves storage and makes no NVS erase attempt.
An unhandled panic, watchdog reset, or power loss requires no application
action: the bootloader sees the unconfirmed pending image and rolls back.

The existing failure-streak/recovery breaker remains for non-pending images.
It is not allowed to trap a pending image in repeated recovery boots instead
of rolling it back.

## Tests and HIL plan

Unit seams cover pending-state discovery, mark-valid, rollback request on a
controlled local failure, and no-op valid boot. CallBox qualification tests
cover success when every required heartbeat advances and failure when one
required task does not.

Hardware-in-loop procedure:

1. Flash known-good A and a test B through the Phase 3 service; activate B.
2. Verify B reports `PENDING_VERIFY`, completes the fifteen-second local window,
   and then remains selected after reset.
3. Inject one startup failure in B and verify an immediate rollback to A with
   configuration and sequence high watermark preserved.
4. Hold/reset/panic/WDT B before qualification and verify the next boot is A.
5. Run B with Wi-Fi unavailable and with broker unreachable; verify local
   workers qualify B without waiting for infrastructure connectivity.
6. Exercise recovery boot qualification, then repeat the forced-failure case.

## Explicitly deferred

- WebUI OTA routes, upload/install/discard presentation, and AP upload policy;
- CallBox OTA admission policy, server/HTTPS source, WCS trigger, and cancel
  button gesture;
- tower/buzzer OTA output patterns;
- anti-rollback eFuse / secure-version release policy;
- changes to partition migration or persistent-storage behavior.

## Executed HIL evidence

Phase 4 was exercised on the ESP32-S3 target on COM15 with the production
16 MiB partition map and rollback-enabled production bootloader.

- Unity HIL: 6/6 groups passed for pending detection, mark-valid, controlled
  rollback request, valid-image no-op, healthy heartbeat qualification, and
  missing-heartbeat rejection.
- Good-image A/B path: production CallBox image in `ota_0` at `0x2A0000`
  booted as the selected OTA image, preserved configuration and sequence
  storage, reached Wi-Fi/MQTT/WCS ready state, and logged
  `Pending OTA image qualified and marked valid (normal mode)` after the
  bounded qualification window.
- Failure-image path: a deliberately resetting valid ESP32-S3 application was
  written to `ota_1` at `0x6A0000` and selected as a NEW image. After the
  pre-validation reset, bootloader `otadata` recorded `ota_1` as
  `ESP_OTA_IMG_ABORTED (0x4)` while `ota_0` remained
  `ESP_OTA_IMG_VALID (0x2)`.
- Post-rollback regression: the next boot loaded the production image from
  `0x2A0000`; CallBox configuration remained intact, sequence advanced from
  the persisted high watermark, Wi-Fi obtained `192.168.1.107`, MQTT became
  operational, WCS sync was accepted, and `Callbox ready` was reached.

This proves both the positive `PENDING_VERIFY -> VALID` path and the negative
unconfirmed-image `reset -> ABORTED -> previous VALID slot` path on hardware.