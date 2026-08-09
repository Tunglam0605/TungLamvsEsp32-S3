# Platform

Platform là wrapper generic cho provider/system service, khác BSP là board hardware. Platform gọi ESP-IDF nhưng không biết CallBox, Mission, MQTT, portal hoặc NVS keys product.

- [platform_wifi](platform_wifi/README.md): Wi-Fi/netif/event provider.
- [platform_nvs](platform_nvs/README.md): primitive NVS typed.
- [platform_time](platform_time/README.md): SNTP/time provider.
