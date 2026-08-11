# Components architecture

`main` chi chua cac tang dung chung:

```text
Product branch -> BSP -> Drivers
               -> Platform
```

| Layer | Responsibility |
|---|---|
| BSP | Tai nguyen va giao dien phan cung cua board |
| Drivers | Driver IC generic, khong biet ten san pham |
| Platform | Provider Wi-Fi, NVS, time cua ESP-IDF |

Callbox va Gateway la product component o nhanh rieng; khong nam tren `main`.
