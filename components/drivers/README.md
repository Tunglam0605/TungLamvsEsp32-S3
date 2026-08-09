# Drivers

Driver là abstraction IC generic, không biết board hay nghiệp vụ. Driver chỉ phụ thuộc ESP-IDF peripheral APIs; caller/BSP chịu ownership bus, locking và active-low semantics.

- [tca9554](tca9554/README.md): I/O expander 8-bit.
