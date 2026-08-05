# ADR-003: BSP owns the I2C bus

`bsp_i2c` creates one ESP-IDF I2C master bus for GPIO41/GPIO42. TCA9554 and a
future RTC attach to this bus. The TCA9554 driver cannot create or configure a
bus itself.
