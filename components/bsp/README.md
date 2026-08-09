# BSP — Waveshare ESP32-S3-POE-ETH-8DI-8DO

BSP là lớp hardware semantic, không biết Mission, MQTT, portal hay task nghiệp vụ. Nó khởi tạo board, đọc tám DI active-low, điều khiển tám DO qua TCA9554 và cung cấp Ethernet/buzzer board.

| Module | Responsibility |
|---|---|
| bsp_board | init toàn board |
| bsp_di | GPIO4–11, đọc DI raw |
| bsp_do | DO shadow register, all-off safe state |
| bsp_expander | TCA9554 at 0x20, I2C1 SDA42/SCL41, 400 kHz |
| bsp_buzzer | GPIO46 feedback buzzer |
| bsp_eth | W5500 board transport |

Mapping DI/DO sang Cancel/Task/LED chỉ nằm ở CallBox callbox_io.c. BSP không được thêm logic task hoặc policy AP.

