# Verification status

| Area | Status | Evidence / limitation |
|---|---|---|
| Windows build | COMPILED; release parity not verified | On 2026-08-06, `set-target`, `fullclean`, and `build` passed with the v6.0.1 root tag (`8c19b156084a0753687347cca1f5355782893533`) and separately installed v6.0.1 tools. The current Stage 1 firmware image is 219,856 bytes; its committed-image reconfigure and build also passed before flash. The local shallow ESP-IDF clone's incomplete unused submodules still prevent a pristine-release-parity claim. See [Windows record](windows-clean-build.md). |
| Unit test | VERIFIED in CI | `tests/host/bsp_do_state_test.c` covers active-high/active-low translation, nonzero safe masks, desired-versus-applied state, and `applied_valid` before/after commit. [GitHub Actions run 31068212648](https://github.com/Tunglam0605/TungLamvsEsp32-S3/actions/runs/31068212648) passed configure, build, and CTest at head `e4dcff4`. This Windows shell has no native host compiler with the required Windows linker libraries. |
| Hardware test | PARTIAL HIL | The Stage 1 run at `e4dcff4` flashed COM15, captured a successful boot log, confirmed runtime 16 MiB Flash/8 MiB PSRAM, initialized I2C/TCA9554, and applied logical safe state with `applied_valid=true`. The operator heard the buzzer test. DI actuation, RGB observation, physical DO measurement, and reset/power-cycle testing were not completed; no pin is marked `VERIFIED_HARDWARE_TEST`. See [HIL-20260806-board-unidentified](hil-results/HIL-20260806-board-unidentified.md). |
| CI | VERIFIED | [GitHub Actions run 31068212648](https://github.com/Tunglam0605/TungLamvsEsp32-S3/actions/runs/31068212648) passed both jobs at head `e4dcff4`: the `espressif/idf:v6.0.1` container ran `idf.py set-target esp32s3`, `idf.py fullclean`, and `idf.py build`; the independent Ubuntu host-test job passed CTest. |

HIL must record the exact board revision, Flash/PSRAM detection, DI raw state
while actuating each input, RGB/buzzer observation, and output measurement
before changing any provisional setting or verification state.

The ESP-IDF `app_update` archive appears as a small transitive dependency of
the core partition support. It is not an OTA feature in this firmware: the
partition table has one factory application partition, no OTA data/slots, and
the source contains no OTA API, transport, or update workflow.
