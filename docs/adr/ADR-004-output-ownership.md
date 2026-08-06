# ADR-004: BSP owns digital outputs

Only `bsp_do` writes the TCA9554 output register during runtime. It maintains
logical `desired_mask`, `applied_mask`, and `safe_mask`; applications cannot
write the expander directly. `bsp_do_get_status()` returns these values with
`applied_valid`: `applied_mask` becomes valid only after a successful TCA9554
write. Failed writes leave the previously applied state unchanged.

The safe mask and output polarity are logical/provisional configuration, not
claims about reset or power-on electrical behavior. HIL must verify the
transient behavior before a product depends on it.
