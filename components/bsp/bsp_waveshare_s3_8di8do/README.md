# bsp_waveshare_s3_8di8do

Board support for the Waveshare ESP32-S3-POE-ETH-8DI-8DO. It owns the board's
GPIO/I2C configuration, TCA9554 digital outputs, and safe-state policy. It
does not initialize network or product behavior.

DI and DO polarity settings are explicitly provisional until hardware-in-loop
testing records the behavior of the connected board.

Public headers expose board, DI, DO, RGB, buzzer, and BOOT APIs only. The
ESP-IDF I2C bus handle is private to this component and is passed to
`drv_tca9554` internally. DI reads return `esp_err_t` plus an output value so
a failed read cannot be mistaken for a logical low; DO status includes desired,
applied, safe, and `applied_valid` state.
