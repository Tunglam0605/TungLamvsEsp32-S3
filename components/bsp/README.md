# BSP — Waveshare ESP32-S3-POE-ETH-8DI-8DO

BSP là lớp hardware semantic, không biết Mission, MQTT, portal hay task nghiệp vụ. Nó khởi tạo board, đọc tám DI active-low, điều khiển tám DO qua TCA9554 và cung cấp Ethernet/buzzer board.

| Module | Responsibility |
|---|---|
| bsp_board | init toàn board |
| bsp_di | GPIO4–11, đọc DI raw |
| bsp_i2c | owns I2C_NUM_1, SCL GPIO41, SDA GPIO42 |
| bsp_do | TCA9554 0x20/400 kHz, active-low, mutex/shadow, all-off 0xFF |
| bsp_buzzer | GPIO46 feedback buzzer |
| bsp_eth | W5500 board transport |

TCA9554 là driver generic tại components/drivers/tca9554; bsp_do consume driver và bus do bsp_i2c sở hữu. Mapping DI/DO sang Cancel/Task/LED chỉ nằm ở CallBox callbox_io.c. BSP không được thêm logic task hoặc policy AP.
