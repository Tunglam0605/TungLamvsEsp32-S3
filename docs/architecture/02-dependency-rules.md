# Dependency rules

- Applications use BSP public APIs and never access GPIO, I2C, or TCA9554.
- The BSP creates and owns the board I2C bus.
- The TCA9554 driver receives a bus handle and never creates a bus.
- Drivers do not know board labels, business behavior, MQTT, or FreeRTOS
  application tasks.
- BSP code does not start application, MQTT, HTTP, Wi-Fi, or Ethernet tasks.
