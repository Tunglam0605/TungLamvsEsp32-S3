# Dependency rules

- Applications use BSP public APIs and never access GPIO, I2C, or TCA9554.
- The I2C bus handle is private to the BSP; public BSP headers do not include
  `driver/i2c_master.h` or expose `bsp_i2c_*`.
- The BSP creates and owns the board I2C bus.
- The TCA9554 driver receives a bus handle and never creates a bus.
- Drivers do not know board labels, business behavior, MQTT, or FreeRTOS
  application tasks.
- BSP code does not start application, MQTT, HTTP, Wi-Fi, or Ethernet tasks.
