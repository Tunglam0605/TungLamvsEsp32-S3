# TCA9554 driver

Driver generic cho expander I2C TCA9554. BSP cấp I2C bus, address và diễn giải P0…P7; driver chỉ thực hiện config/output register bằng ESP-IDF i2c_master API.

Quy ước active-low và DO shadow/mutex là contract BSP DO, không phải business policy driver.

