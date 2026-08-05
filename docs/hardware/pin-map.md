# Pin map

## Verification states

Each GPIO or expander output has exactly one current evidence status:

- `VERIFIED_VENDOR_DOC`: stated by the Waveshare product page or Wiki.
- `VERIFIED_VENDOR_DEMO`: exercised by the official Waveshare Arduino demo.
- `VERIFIED_HARDWARE_TEST`: observed using this repository's firmware on a
  physical board. This state is intentionally not yet assigned.
- `DOCUMENT_CONFLICT`: available sources disagree; implementation is blocked.
- `RESERVED`: deliberately unavailable to Phase 0-1 application code.
- `NOT_VERIFIED`: no claim is made about the physical signal.

`VERIFIED_VENDOR_DEMO` means the demo is the strongest available source; it is
not a substitute for hardware-in-the-loop (HIL) verification.

Sources: [Waveshare product page](https://www.waveshare.com/esp32-s3-poe-eth-8di-8do.htm),
[Wiki](https://www.waveshare.com/wiki/ESP32-S3-POE-ETH-8DI-8DO), and the
[official demo archive](https://files.waveshare.com/wiki/ESP32-S3-POE-ETH-8DI-8DO/ESP32-S3-POE-ETH-8DI-8DO-Demo.zip).

## ESP32-S3 GPIOs

| GPIO | Signal / source claim | State | Phase 0-1 disposition |
|---:|---|---|---|
| GPIO0 | BOOT button | VERIFIED_VENDOR_DOC | BSP boot input; pressed polarity is provisional. |
| GPIO1 | Analog-capable; no board function implemented | RESERVED | Do not use. |
| GPIO2 | CAN RX | VERIFIED_VENDOR_DEMO | Reserved; CAN is out of scope. |
| GPIO3 | CAN TX | VERIFIED_VENDOR_DEMO | Reserved; CAN is out of scope. |
| GPIO4 | DI1 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO5 | DI2 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO6 | DI3 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO7 | DI4 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO8 | DI5 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO9 | DI6 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO10 | DI7 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO11 | DI8 | VERIFIED_VENDOR_DEMO | BSP DI; logical polarity is provisional. |
| GPIO12 | W5500 INT | VERIFIED_VENDOR_DEMO | Reserved; Ethernet is out of scope. |
| GPIO13 | W5500 MOSI | VERIFIED_VENDOR_DEMO | Reserved; Ethernet is out of scope. |
| GPIO14 | W5500 MISO | VERIFIED_VENDOR_DEMO | Reserved; Ethernet is out of scope. |
| GPIO15 | W5500 SCLK | VERIFIED_VENDOR_DEMO | Reserved; Ethernet is out of scope. |
| GPIO16 | W5500 CS | VERIFIED_VENDOR_DEMO | Reserved; Ethernet is out of scope. |
| GPIO17 | RS485 TX | VERIFIED_VENDOR_DEMO | Reserved; RS485 is out of scope. |
| GPIO18 | RS485 RX | VERIFIED_VENDOR_DEMO | Reserved; RS485 is out of scope. |
| GPIO19 | USB D- in supplied pin table | RESERVED | No Phase 0-1 use; vendor electrical confirmation pending. |
| GPIO20 | USB D+ in supplied pin table | RESERVED | No Phase 0-1 use; vendor electrical confirmation pending. |
| GPIO21 | RS485 RTS in Wiki/demo; SD CS in supplied table | DOCUMENT_CONFLICT | Reserved; no RS485 or TF implementation. |
| GPIO22 | No verified board function | RESERVED | Do not infer availability. |
| GPIO23 | No verified board function | RESERVED | Do not infer availability. |
| GPIO24 | No verified board function | RESERVED | Do not infer availability. |
| GPIO25 | No verified board function | RESERVED | Do not infer availability. |
| GPIO26 | No verified board function | RESERVED | Do not infer availability. |
| GPIO27 | No verified board function | RESERVED | Do not infer availability. |
| GPIO28 | No verified board function | RESERVED | Do not infer availability. |
| GPIO29 | No verified board function | RESERVED | Do not infer availability. |
| GPIO30 | No verified board function | RESERVED | Do not infer availability. |
| GPIO31 | No verified board function | RESERVED | Do not infer availability. |
| GPIO32 | No verified board function | RESERVED | Do not infer availability. |
| GPIO33 | Internal occupancy in supplied table | RESERVED | Do not use without exact schematic. |
| GPIO34 | Internal occupancy in supplied table | RESERVED | Do not use without exact schematic. |
| GPIO35 | Internal occupancy in supplied table | RESERVED | Do not use without exact schematic. |
| GPIO36 | Internal occupancy in supplied table | RESERVED | Do not use without exact schematic. |
| GPIO37 | Internal occupancy in supplied table | RESERVED | Do not use without exact schematic. |
| GPIO38 | WS2812 RGB data | VERIFIED_VENDOR_DEMO | BSP RGB; waveform HIL pending. |
| GPIO39 | W5500 reset | VERIFIED_VENDOR_DEMO | Reserved; Ethernet is out of scope. |
| GPIO40 | RTC interrupt | VERIFIED_VENDOR_DOC | Reserved; RTC is out of scope. |
| GPIO41 | I2C SCL | VERIFIED_VENDOR_DEMO | BSP-owned I2C bus, shared with TCA9554/RTC. |
| GPIO42 | I2C SDA | VERIFIED_VENDOR_DEMO | BSP-owned I2C bus, shared with TCA9554/RTC. |
| GPIO43 | UART0 TX in supplied table | RESERVED | No Phase 0-1 use. |
| GPIO44 | UART0 RX in supplied table | RESERVED | No Phase 0-1 use. |
| GPIO45 | TF card MISO | VERIFIED_VENDOR_DEMO | Reserved; TF is out of scope. |
| GPIO46 | Buzzer | VERIFIED_VENDOR_DEMO | BSP buzzer; electrical behavior HIL pending. |
| GPIO47 | TF card MOSI | VERIFIED_VENDOR_DEMO | Reserved; TF is out of scope. |
| GPIO48 | TF card SCLK | VERIFIED_VENDOR_DEMO | Reserved; TF is out of scope. |

## TCA9554 expander outputs

| Output | I2C address | State | Phase 0-1 disposition |
|---|---:|---|---|
| EXIO1 / DO1 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |
| EXIO2 / DO2 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |
| EXIO3 / DO3 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |
| EXIO4 / DO4 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |
| EXIO5 / DO5 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |
| EXIO6 / DO6 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |
| EXIO7 / DO7 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |
| EXIO8 / DO8 | 0x20 | VERIFIED_VENDOR_DEMO | BSP DO, provisional active level. |

Do not promote any row to `VERIFIED_HARDWARE_TEST` until the flash/monitor
log and the board revision used for the observation are recorded.
