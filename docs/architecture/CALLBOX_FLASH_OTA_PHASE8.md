# CallBox Flash/OTA - Phase 8: Cancel 10 s OTA Trigger

## Scope

Phase 8 adds only a CallBox product trigger. It does not add another OTA engine and does not change the frozen flash layout, NVS schema, generic OTA service, or platform OTA provider.

## Gesture contract

```text
release < 5 s              -> normal CANCEL
5 s continuously held      -> toggle Rescue AP once
6, 7, 8, 9 s               -> one warning beep event at each milestone
10 s continuously held     -> warning beep + one HTTPS OTA request
release at/after 5 s       -> never emit normal CANCEL
```

`cancel_hold_gesture` is a pure monotonic-time recognizer. Each milestone is one-shot per hold and release/repress resets the gesture. Release also flushes any milestone crossed since the last 20 ms Mission tick; this closes the scheduling boundary race at 5 s and 10 s.

## Dependency direction

```text
Cancel input
  -> cancel_hold_gesture
  -> Mission Manager side-effect adapter
      -> Rescue AP API
      -> status feedback -> Output Renderer -> buzzer owner
      -> ota_https_source_request_configured()
           -> ota_policy
           -> HTTPS source
           -> generic ota_service
           -> platform_ota
```

The trigger never calls `esp_ota_*`, never calls the generic OTA service directly, and never installs or reboots. Phase 7 still owns HTTPS transport and Phase 5 still owns admission policy.

If server OTA is disabled or the manifest URL is absent/insecure, the 10-second request is safely declined and logged. The gesture remains consumed and is not retried until a new press. A deployment HTTPS manifest endpoint must be provisioned before remote download can occur.

## Executed validation

- Development firmware build passed: `callbox_sews.bin = 0x126c90`; 42% remains free in the 2 MiB factory partition.
- Unity HIL on COM15 passed 22/22: six rollback/boot-validation tests, four product policy tests, five HTTPS parser tests, and seven Cancel-hold gesture tests.
- The gesture suite covers short cancel, one-shot 5 s rescue, one-shot 6-10 s warnings, no OTA before 10 s, one-shot 10 s OTA request, release/repress reset, release-edge milestone flushing, and no retry after dispatch failure.
- A real Phase 8 candidate was written only to inactive `ota_0`, selected as NEW, booted from `0x2A0000`, and qualified to VALID. `otadata` after qualification: `seq=3/state=VALID` for `ota_0`, while the previous `ota_1 seq=2/state=VALID` remained available.
- Persistent state survived: CallBox ID/config remained present, sequence advanced from 3861 to 3925, Wi-Fi connected, MQTT became operational, WCS sync was accepted, and CallBox reached ready state.
- No partition-table or NVS erase/change was performed.

## Deployment note

`CONFIG_CALLBOX_OTA_HTTPS_ENABLE` remains fail-safe OFF by default and the compile-time manifest URL is empty until a real deployment endpoint is supplied. Phase 8 wiring is complete; enabling the server source is a deployment configuration step, not a change to the gesture or OTA architecture.
