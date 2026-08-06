# Verification status

| Area | Status | Evidence / limitation |
|---|---|---|
| Windows build | COMPILED; release parity not verified | On 2026-08-06, `set-target`, `fullclean`, and `build` passed with the v6.0.1 root tag (`8c19b156084a0753687347cca1f5355782893533`) and separately installed v6.0.1 tools. It produced one 217,776-byte firmware. The local shallow ESP-IDF clone reported `v6.0.1-dirty` because several unused submodules were incomplete; this cannot verify pristine-release parity. See [Windows record](windows-clean-build.md). |
| Unit test | VERIFIED in CI | `tests/host/bsp_do_state_test.c` covers active-high/active-low translation, nonzero safe masks, desired-versus-applied state, and `applied_valid` before/after commit. GitHub Actions run `31066436725` passed configure, build, and CTest at head `1066e43`. This Windows shell still has no native C compiler on `PATH`. |
| Hardware test | FLASHED; HIL NOT VERIFIED | A firmware image from head `1066e43` was written successfully on 2026-08-06; esptool identified ESP32-S3 revision v0.2 with embedded 8 MB PSRAM and verified all written hashes. No complete application boot log or physical RGB/buzzer/DI/DO observation was captured. Flash success is not HIL evidence, and no pin is marked `VERIFIED_HARDWARE_TEST`. The operator will perform the controlled power-cycle/monitor procedure and supply the log for review. |
| CI | VERIFIED | GitHub Actions run `31066436725` passed both jobs at head `1066e43`: the `espressif/idf:v6.0.1` container ran `idf.py set-target esp32s3`, `idf.py fullclean`, and `idf.py build`; the independent Ubuntu host-test job passed CTest. The former `espressif/idf:v6.1.0` job could not start because its image tag had no manifest. |

HIL must record the exact board revision, Flash/PSRAM detection, DI raw state
while actuating each input, RGB/buzzer observation, and output measurement
before changing any provisional setting or verification state.

The ESP-IDF `app_update` archive appears as a small transitive dependency of
the core partition support. It is not an OTA feature in this firmware: the
partition table has one factory application partition, no OTA data/slots, and
the source contains no OTA API, transport, or update workflow.
