# Platform overview

This repository is a reusable firmware platform for Waveshare
ESP32-S3-POE-ETH-8DI-8DO hardware. Product-specific code belongs in a product
repository and must consume a versioned platform API.

Phase 0-1 provides only the board foundation and one diagnostic application.
It does not implement connectivity, provisioning, field protocols, or any
Callbox/WCS/AGV behavior.

The Phase 0-1 capability structure exposes only DI, DO, buzzer, and RGB. It
does not advertise unimplemented network, wireless, fieldbus, RTC, or TF-card
features merely because the board may physically contain them.

The root CMake `COMPONENTS` allow-list likewise includes only `main`,
`board_hardware_test`, the Waveshare BSP, and `drv_tca9554` plus their declared
ESP-IDF dependency closure.
