# CallBox Flash/OTA - Phase 6: Authenticated WebUI OTA

## HTTP contract

The existing AP/STA configuration server now exposes one authenticated OTA
page and four authenticated APIs:

- `GET /ota`
- `GET /api/ota/status`
- `POST /api/ota/upload` with `application/octet-stream`
- `POST /api/ota/install`
- `POST /api/ota/discard`

The handlers reuse the portal's existing `cb_auth` cookie and 30-minute login
lifetime. There is no unauthenticated firmware upload path on either AP or STA.

## Streaming and failure semantics

Upload is streamed in fixed 4096-byte chunks directly into the same generic
OTA service used by every source. Firmware is never buffered as a complete
image in RAM. Three consecutive HTTP receive timeouts abort the transaction;
a disconnected client also aborts it. Generic prefix/final image validation
remains authoritative.

The configuration AP is explicitly held active while a browser upload is in
progress. This hold is included in the existing portal-session predicate used
by AP lifecycle policy and is released on every success/failure exit.

Upload success means only `STAGED`. `Install & Reboot` is a separate policy
gate: the product handler selects the staged boot partition through
`ota_service_install()`, sends the response, then performs the product-level
restart. Rollback/boot validation from Phase 4 validates the next boot.

## UI

`/ota` provides file selection, upload progress, service-state polling, staged
image metadata, Install & Reboot, and Discard. The existing configuration page
links to this maintenance page.

## Executed HIL evidence

- Production build passed: `callbox_sews.bin` = `0x126920`; 42% remains free in the 2 MiB factory partition.
- The Phase 6 candidate was written only to inactive `ota_1` (`0x6A0000`) and selected as NEW. It booted from `ota_1`; Phase 4 boot validation marked it VALID (`0x2`).
- Persistent configuration and sequence state were preserved; Wi-Fi obtained `192.168.1.107`, MQTT became operational, WCS sync completed, and CallBox reached ready state.
- Authenticated STA access to `/api/ota/status` returned `idle`.
- A real `application/octet-stream` upload streamed 1,206,560 bytes through the HTTP handler into the generic OTA service, reached `STAGED`, then `DISCARD` returned the service to `IDLE`.
- Invalid firmware returned HTTP 400 and `FAILED`; discard recovered to `IDLE`. Unsupported content type returned HTTP 415 without starting OTA.
- Deliberate TCP disconnects after approximately 10% and 90% of a declared firmware body both aborted the transaction and returned the OTA service to `IDLE`, proving re-entry is safe after interrupted uploads.
