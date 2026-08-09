# BSP — Waveshare ESP32-S3-POE-ETH-8DI-8DO

BSP sở hữu board initialization, DI, DO, I2C private, W5500 Ethernet và buzzer onboard. BSP không biết Task1/Task2/Cancel, Mission, MQTT hoặc WCS.

Init order: DI → private I2C → DO → GPIO46 buzzer.

- DI GPIO4–GPIO11, active-low; product mapping thuộc CallBox.
- I2C private: I2C_NUM_1, SCL GPIO41, SDA GPIO42; application không nhận raw bus handle.
- DO: TCA9554 address 0x20, 400 kHz, timeout 100 ms; mutex BSP-owned, transactional shadow, safe all-off 0xFF. Active-low xử lý BSP-side.
- W5500: INT12, MOSI13, MISO14, SCLK15, CS16, RESET39; SPI2 20 MHz.
- Buzzer GPIO46/LEDC là feedback mạng, tách DO1.
