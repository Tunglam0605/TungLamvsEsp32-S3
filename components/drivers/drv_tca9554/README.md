# drv_tca9554

Low-level ESP-IDF driver for the TCA9554 I2C I/O expander. The driver owns only
its device handle and lock; the caller provides an already-created I2C master
bus. It has no Waveshare, output-pattern, or application knowledge.
