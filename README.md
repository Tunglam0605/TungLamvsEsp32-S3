# AUBOT ESP32-S3 Gateway

Firmware Gateway cho board Waveshare ESP32-S3-POE-ETH-8DI-8DO.

## Pham vi hien tai

- Wi-Fi AP + STA, STA mac dinh ket noi `Robotics AUBOT 1` bang DHCP.
- AP commissioning: `AUBOT-{gateway_id}-{MAC6}`, IP `192.168.65.204/24`.
- Ethernet W5500: debug máy tính `192.168.66.204/24`, hoặc uplink
  DHCP/IP tĩnh theo cấu hình WebUI.
- CAN Classic cach ly: 250 kbps, Standard ID 11-bit, TX GPIO2 / RX GPIO3.

Khong co logic nut nhan, den thap, Mission hay giao thuc thiet bi cu trong
firmware Gateway. Protocol Laser/CAN va dich vu uplink se duoc them trong
`components/gateway`.

Build: `idf.py set-target esp32s3` va `idf.py build`.
