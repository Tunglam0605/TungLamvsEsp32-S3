# Layering

```text
Application -> Platform services -> BSP -> Low-level drivers -> ESP-IDF
```

Only the currently implemented path is present in Phase 0-1:

```text
board_hardware_test -> bsp_waveshare_s3_8di8do -> drv_tca9554 -> ESP-IDF
```

The BSP knows Waveshare pin mapping and board policy. `drv_tca9554` only knows
the TCA9554 register protocol and an I2C bus handle.
