# Gateway components

Firmware Gateway chia theo cac tang:

```text
Product branch -> BSP -> Drivers
               -> Platform
```

| Layer | Responsibility |
|---|---|
| BSP | Tai nguyen va giao dien phan cung cua board |
| Drivers | Driver IC generic, khong biet ten san pham |
| Platform | Provider Wi-Fi, NVS, time cua ESP-IDF |

`gateway` la lop product duy nhat tren nhanh nay; no su dung BSP va Platform
nhung khong dua giao thuc nghiep vu vao cac lop dung chung.
