# `callbox_sews` reference assessment

`C:\Users\Admin\Documents\ESP32\callbox_sews` was reviewed as a reference
implementation for Windows setup and hardware history only. It is not an
architectural base for this repository.

| Reference item | Assessment | Reuse decision / destination |
|---|---|---|
| `dependencies.lock` | Declares ESP-IDF `6.1.0`, target `esp32s3`. | Historical environment evidence only; it does not select this platform's v6.0.1 stable toolchain. |
| root `CMakeLists.txt` | A conventional ESP-IDF project setup. | Not copied; this project has explicit Phase 0-1 component directories. |
| `build-win` log | Shows a historical Windows build with `C:\Espressif\v6.1-dev\esp-idf`, CMake 4.0.3, and Xtensa 15.2. | Environment clue only; no generated files or cache reused. |
| `sdkconfig` | Uses 2 MB DIO Flash and no PSRAM. | Rejected: conflicts with the current N16R8 expected profile. |
| legacy TCA9554/I2C code | Creates a legacy I2C bus in the driver; duplicate/newer code has inconsistent I2C-port ownership. | Rejected: new `drv_tca9554` receives a BSP-owned I2C bus and has no application dependency. |
| legacy DI/button logic | Uses an active-low convention. | Not copied: converted only into an explicit provisional Kconfig assumption in BSP, with raw-state logging required for HIL. |
| LED / Callbox / AGV / WCS / MQTT code | Product/business logic or wrong-layer dependencies. | Rejected and absent. |

No source file or code block from `callbox_sews` was copied into this project.
The resulting source was written around the current layer rules; vendor
documentation/demo, not Callbox code, is the direct source for the current
pin and peripheral mapping. Build artifacts (`build/`, `build-win/`, `.elf`,
`.bin`, `.map`, CMake cache, logs), credentials, and machine-specific
`sdkconfig` were not reused.
