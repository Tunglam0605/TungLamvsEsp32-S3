# CallBox Flash/OTA - Phase 7: HTTPS Server Source

## Scope

Phase 7 adds `ota_https_source`, a CallBox product/source adapter over the
existing generic `ota_service`. It contains no `esp_ota_*` use, GPIO, tower,
or buzzer control. `platform_ota` remains the sole ESP-IDF OTA provider.

## Request and policy

`ota_https_source_request_configured()` is a small asynchronous API intended
for the Phase 8 internal/server trigger. It starts a low-priority worker and
returns immediately, so it never blocks the mission task. The manifest URL is
compile-time configuration (`CONFIG_CALLBOX_OTA_HTTPS_MANIFEST_URL`), not
`Config_t` or NVS. The feature is off by default through
`CONFIG_CALLBOX_OTA_HTTPS_ENABLE`.

Before touching the network, the worker asks `ota_policy` to begin with
`OTA_POLICY_SOURCE_INTERNAL_TRIGGER`. Thus normal mode retains the existing
idle/no-pending/`COMM_READY` admission rules and recovery rejects this remote
source.

## Transport and TLS

Both manifest and firmware requests require an `https://` URL. Requests use
ESP-IDF 6.1 `esp_http_client` with `esp_crt_bundle_attach`, default hostname/CN
checking (`skip_cert_common_name_check = false`), and automatic redirects
disabled. Any non-200 response, redirect, malformed URL, or TLS failure is
rejected. Certificate-bundle defaults are enabled without enabling insecure
ESP-TLS options.

## Manifest contract

The response is bounded to 1024 bytes and must be one strict JSON object:

```json
{"firmware_url":"https://updates.example/callbox.bin","project":"callbox_sews","version":"1.2.3","size":1234567}
```

`firmware_url`, `project`, and `version` are required non-empty plain JSON
strings. `size` is optional and, when present, is a positive integer. Duplicate
or unknown fields, escapes/control characters in these contract strings,
trailing data, HTTP firmware URLs, and malformed JSON are rejected. The source
checks `project` against the running app before beginning an OTA stream. Version
and project are checked again against the staged image descriptor. Version may
be the same or newer; no downgrade comparison is imposed by this phase. The
generic service remains authoritative for image validation.

## Streaming and install boundary

The image is read in fixed 4096-byte chunks and passed directly to
`ota_service_begin/write/finish`; no complete firmware is held in RAM. If a
declared manifest size is present it must equal the HTTP Content-Length. A
successful download is only `STAGED`. This source never calls install, selects
a boot partition, or restarts the device.

## Tests

Private pure-parser seams cover valid manifests, malformed JSON, missing
fields, project mismatch handling, HTTP URL rejection, and optional-size
parsing. Transport and task code stay outside these deterministic parser tests.

## Executed HIL evidence

- Clean production build passed with `callbox_sews.bin` = `0x126970` (1,206,640 bytes), leaving about 42% free in the smallest app partition.
- Unity HIL on COM15 passed 15/15: six rollback/boot-validation tests, four product OTA-policy tests, and five HTTPS manifest/parser tests.
- Architecture audit found no direct `esp_ota_*` use outside `platform_ota`, no HTTP fallback, no disabled hostname verification, and no install/reboot call inside the HTTPS source.
- The transfer buffer is bounded and static because the HTTPS worker is a singleton, avoiding a 4 KiB payload buffer on the worker stack while preserving streaming semantics.
