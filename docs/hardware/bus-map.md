# Bus map

## I2C

The Phase 0-1 BSP creates one synchronous I2C master bus on GPIO41 (SCL) and
GPIO42 (SDA). It uses ESP-IDF I2C controller 0 as a software allocation; that
controller number is not a board electrical fact. The bus owns:

- TCA9554PWR at `0x20` (implemented);
- board RTC (reserved for a later phase).

The current 100 kHz device speed is deliberately conservative and follows the
vendor Arduino demo's default `Wire` speed. It can only change after hardware
validation.

## SPI and SD/MMC

W5500 and TF-card pin facts are documented in [pin-map.md](pin-map.md), but
neither bus is initialized in Phase 0-1. GPIO21's RS485/SD-CS conflict prevents
any conclusion about simultaneous RS485 and TF-card use.
