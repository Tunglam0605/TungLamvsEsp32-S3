# Safe state

The Phase 0-1 safe state is logical DO1..DO8 OFF (`safe_mask = 0x00`).

`bsp_do` owns three *logical* masks:

- `desired_mask`: most recent requested logical outputs;
- `applied_mask`: most recently successfully written logical outputs;
- `safe_mask`: logical outputs required during initialization and test exit.

`applied_valid` is false until the first successful I2C output write commits
an applied mask. A requested value can therefore differ from the last known
applied value after a write failure.

The TCA9554 register value is produced only by the BSP. It is not assumed that
logical OFF always equals a zero register bit: the provisional Kconfig setting
`PLATFORM_DO_ACTIVE_HIGH_PROVISIONAL` controls the translation. `applied_mask`
is updated only after a successful I2C write.

The BSP latches the translated safe value before changing TCA9554 directions to
output mode. Reset-time expander behavior, output transient behavior, and the
actual output active level are not HIL-verified.
