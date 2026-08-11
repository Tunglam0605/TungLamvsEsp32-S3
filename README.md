# ESP32-S3 Common Base

`main` la nen dung chung cho Waveshare ESP32-S3-POE-ETH-8DI-8DO.

```text
main                 BSP, driver generic, platform va firmware base
callbox-esp32-s3     firmware Callbox rieng
gateway-esp32-s3     firmware Gateway rieng
```

Main khong chua MQTT/WCS, portal, mapping nut, Laser protocol hay logic nghiep
vu san pham. Firmware base chi khoi tao NVS, ESP-NETIF, event loop va BSP.

## Thanh phan dung chung

- `components/bsp`: DI, DO, buzzer, W5500 Ethernet, CAN Classic.
- `components/drivers`: driver IC generic.
- `components/platform`: Wi-Fi, NVS va time provider.
- `main`: composition root toi thieu.

Build: `idf.py set-target esp32s3` va `idf.py build`.
