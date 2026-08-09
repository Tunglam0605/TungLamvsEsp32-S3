# Components architecture

    main
      ↓
    CallBox
     ├─ Platform
     └─ BSP
         └─ Driver

| Layer | Responsibility | May depend on | Must not depend on | Representative modules |
|---|---|---|---|---|
| [CallBox](callbox/README.md) | Product runtime/policy | BSP, Platform, ESP-IDF APIs | main | Mission, MQTT, portal, Wi-Fi policy |
| [BSP](bsp/README.md) | Board-level hardware abstraction | driver, ESP-IDF | CallBox/MQTT policy | DI, DO, buzzer, Ethernet |
| [Drivers](drivers/README.md) | Generic IC access | ESP-IDF | BSP semantic names, CallBox | TCA9554 |
| [Platform](platform/README.md) | Generic service providers | ESP-IDF | CallBox/BSP | Wi-Fi, NVS, time |

Các mũi tên chỉ đi xuống. Platform không biết profile/Rescue AP; BSP không biết Task/MQTT; CallBox không gọi raw TCA9554 hoặc raw DHCP mechanics.

