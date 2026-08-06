# ADR-003: BSP owns the I2C bus

`bsp_i2c` creates one ESP-IDF I2C master bus for GPIO41/GPIO42. TCA9554 and a
future RTC attach to this bus. The TCA9554 driver cannot create or configure a
bus itself. The bus handle and `bsp_i2c_*` functions are private BSP APIs in
`private_include/bsp_i2c_internal.h`; applications use board-level services
only and cannot include the ESP-IDF I2C master handle through a BSP public
header.
