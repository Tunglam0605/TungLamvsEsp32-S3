# bsp_waveshare_s3_8di8do

Board support for the Waveshare ESP32-S3-POE-ETH-8DI-8DO. It owns the board's
GPIO/I2C configuration, TCA9554 digital outputs, and safe-state policy. It
does not initialize network or product behavior.

DI and DO polarity settings are explicitly provisional until hardware-in-loop
testing records the behavior of the connected board.
