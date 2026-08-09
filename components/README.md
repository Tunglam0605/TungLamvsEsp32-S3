# Components

| Layer | Sở hữu | Có thể phụ thuộc | Không được phụ thuộc |
|---|---|---|---|
| [callbox](callbox/README.md) | Product/runtime | BSP, platform, ESP-IDF adapters | main |
| [bsp](bsp/README.md) | Board Waveshare | driver, ESP-IDF | CallBox/MQTT policy |
| [drivers](drivers/README.md) | IC generic | ESP-IDF | BSP/CallBox |
| [platform](platform/README.md) | Provider services | ESP-IDF | CallBox/BSP |

Các dependency direction này là contract kiến trúc.
