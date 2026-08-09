# TCA9554 driver

Driver generic TCA9554 8-pin I/O expander. Public API nằm trong include/tca9554.h: init/deinit device, cấu hình direction/output, read input và write output. Driver không có mutex, không tự tạo I2C bus, không biết DO1/Task/active-low. BSP caller truyền bus/config, serialize access và diễn giải logic output.
