# Verification status

| Area | Status | Evidence / limitation |
|---|---|---|
| Windows build | COMPILED; release parity not verified | On 2026-08-06, `set-target`, `fullclean`, and `build` passed with the v6.0.1 root tag (`8c19b156084a0753687347cca1f5355782893533`) and separately installed v6.0.1 tools. It produced one 217,776-byte firmware. The local shallow ESP-IDF clone reported `v6.0.1-dirty` because several unused submodules were incomplete; this cannot verify pristine-release parity. See [Windows record](windows-clean-build.md). |
| Unit test | NOT VERIFIED locally | `tests/host/bsp_do_state_test.c` covers active-high/active-low translation, nonzero safe masks, desired-versus-applied state, and `applied_valid` before/after commit. This Windows shell has no native C compiler on `PATH`; the independent GitHub Actions host-test job is the authoritative pending run. |
| Hardware test | NOT VERIFIED | No board was flashed or monitored. No pin is marked `VERIFIED_HARDWARE_TEST`. |
| CI | NOT VERIFIED | The former `espressif/idf:v6.1.0` job could not start because the exact image tag had no manifest. This revision uses the exact stable `espressif/idf:v6.0.1` tag plus a native host-test job; it must execute successfully before CI is reported as passed. |

HIL must record the exact board revision, Flash/PSRAM detection, DI raw state
while actuating each input, RGB/buzzer observation, and output measurement
before changing any provisional setting or verification state.

The ESP-IDF `app_update` archive appears as a small transitive dependency of
the core partition support. It is not an OTA feature in this firmware: the
partition table has one factory application partition, no OTA data/slots, and
the source contains no OTA API, transport, or update workflow.
