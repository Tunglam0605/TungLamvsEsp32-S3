# CallBox Flash/OTA - Phase 5: Product Policy and Output Adapter

## Scope

Phase 5 binds the generic OTA service to CallBox product rules without moving
business policy into the OTA core. It adds `ota_policy` and
`ota_output_adapter`; transport/source handlers remain deferred to later phases.

## Admission policy

Normal mode requires both missions IDLE, no call/cancel transaction pending,
`COMM_READY`, and the OTA service in the state required by the requested
action. Local WebUI operations additionally require an authenticated session.

Recovery mode is deliberately narrower: only an authenticated local WebUI may
begin, install, or discard an OTA transaction. WCS/COMM state is not required
because business runtime is not started in recovery mode. Internal/remote
triggers are rejected in recovery mode.

## Output adapter

The generic OTA service still owns no GPIO, tower, buzzer, WebUI, or CallBox
state. `ota_output_adapter` subscribes to immutable OTA events after the OTA
mutex is released and publishes a product snapshot. The existing Output
Renderer consumes the snapshot and gives active OTA transfer/verification/
installation priority over network diagnostics and mission indication.

While OTA is active the tower cycles RED -> YELLOW -> GREEN every 250 ms.
`STAGED` returns the tower to normal CallBox indication. Staged, installing and
failed transitions emit bounded feedback requests through the existing status
store; only the existing Output Renderer owns physical tower/buzzer writes.

## Dependency direction

```text
CallBox ota_policy ---------> status + generic ota_service
CallBox ota_output_adapter -> generic ota_service events + CallBox status
Output Renderer ------------> CallBox ota_output snapshot -> BSP
```

No direct `esp_ota_*` call is added outside `platform_ota`.


## Executed verification

- Production build passed with `callbox_sews.bin` size `0x124860`; the 2 MiB factory partition retains 43% free space.
- Hardware Unity regression on COM15 passed 10/10 groups: the six Phase 4 rollback/health groups plus four product-policy groups.
- Normal policy accepted authenticated IDLE/COMM_READY operation and rejected active missions and COMM_SYNCING.
- Recovery policy accepted authenticated local WebUI operation without WCS and rejected non-local/internal triggers.
- OTA action/state gating was verified for BEGIN and INSTALL.
- After HIL, the original `otadata` was restored and the production `ota_0` image booted from `0x2A0000`; NVS configuration remained intact, sequence advanced `3157 -> 3221`, Wi-Fi obtained `192.168.1.107`, MQTT became operational, WCS sync was accepted, and `Callbox ready` was reached.
