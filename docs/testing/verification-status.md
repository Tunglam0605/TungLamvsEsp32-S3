# Verification status

| Area | Status | Evidence / limitation |
|---|---|---|
| Build | VERIFIED with local ESP-IDF `v6.1-dev` | Windows clean `set-target`, `fullclean`, and `build` passed on 2026-08-05. The final application image is `0x388a0`; exact v6.1.0 release parity remains pending. |
| Unit test | NOT VERIFIED | `tests/host/bsp_do_state_test.c` covers logical-mask translation and commit semantics, but this Windows host has no native C toolchain/SDK to execute it. The ESP-IDF target build does compile `bsp_do_state.c`. |
| Hardware test | NOT VERIFIED | No board was flashed or monitored. No pin is marked `VERIFIED_HARDWARE_TEST`. |
| CI | NOT VERIFIED | Workflow is pinned to `espressif/idf:v6.1.0` and runs a clean build, but no remote workflow run has been observed. |

HIL must record the exact board revision, Flash/PSRAM detection, DI raw state
while actuating each input, RGB/buzzer observation, and output measurement
before changing any provisional setting or verification state.

The ESP-IDF `app_update` archive appears as a small transitive dependency of
the core partition support. It is not an OTA feature in this firmware: the
partition table has one factory application partition, no OTA data/slots, and
the source contains no OTA API, transport, or update workflow.
