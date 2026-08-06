# board_hardware_test

The sole Phase 0-1 application component. It uses only public BSP APIs to
report board identity, Flash/PSRAM profile, BOOT state, raw DI state, and
provisional logical DI state. The optional DO sequence is compile-time disabled
by default and always restores the BSP safe state.
