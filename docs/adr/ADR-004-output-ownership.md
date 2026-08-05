# ADR-004: BSP owns digital outputs

Only `bsp_do` writes the TCA9554 output register during runtime. It maintains
logical `desired_mask`, `applied_mask`, and `safe_mask`; applications cannot
write the expander directly.
