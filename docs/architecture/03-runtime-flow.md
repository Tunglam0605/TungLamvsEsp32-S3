# Runtime flow

1. `main/app_main.c` starts the sole Phase 0-1 application component.
2. `board_hardware_test` initializes the Waveshare BSP.
3. BSP creates the I2C bus, latches a logical-safe DO register value, then
   enables TCA9554 outputs.
4. The application prints identity, flash/PSRAM diagnostics, raw DI state,
   provisional logical DI state, and BOOT state.
5. The application periodically logs DI state changes. The optional DO test is
   disabled by default and always ends in safe state.
