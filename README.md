# Callbox SEWS (ESP32)

Firmware for AGV Callbox System at SEWS Manufacturing Facility.

## Overview

This project implements an MQTT-based callbox system for the AGV (Autonomous Guided Vehicle) network. Each callbox allows line operators to request cart exchange or supply empty carts to the AGV system via push buttons.

**Architecture:**
- 3 illuminated push buttons (Task 1: Exchange, Task 2: Supply Empty, Task 3: Cancel)
- 3-color tower light (Red, Yellow, Green)
- Buzzer for audible feedback
- MQTT communication with WCS (Warehouse Control System)
- NVS-based sequence number persistence for reliable message delivery

## Features

- **MQTT Protocol**: Configurable broker, port and credentials
- **Wi-Fi profiles**: Factory network `AGV1` / `123456789`, plus up to 5 remembered networks
- **Wi-Fi setup portal**: `CALLBOX-<id>` AP opens immediately for phone setup; the STA scans and tries remembered networks in the background
- **LED Feedback**: Task status displayed via button LEDs and tower light
- **Heartbeat Monitoring**: Periodic status publication (configurable interval)
- **Offline Resilience**: Automatic reconnection and configuration AP fallback
- **NVS Storage**: Sequence numbers persisted across power cycles

## File Structure

```
callbox_sews/main/
├── callbox_sews.c          # Main application entry point
├── button_handler.c/h       # Button input handling with debounce
├── led_control.c/h          # LED/buzzer output control
├── mqtt_client.c/h          # MQTT client and event handler
├── state_machine.c/h        # Task state management
├── wifi_init.c/h            # WiFi initialization (STA mode)
├── nvs_storage.c/h          # NVS persistence for seq_num
└── queues.h                 # Shared data structures and queues
```

## MQTT Protocol

### Topics

- **callbox/{id}/event** - Outgoing task events (call, cancel, sync_request)
- **callbox/{id}/cmd** - Incoming commands from WCS (accepted, assigned, locked, completed, etc.)
- **callbox/{id}/status** - Periodic status and heartbeat (retained)

### Button Functions

| Button | Task | Color | Action |
|--------|------|-------|--------|
| Button 1 | Task 1 | Yellow | Exchange cart (full → empty) |
| Button 2 | Task 2 | Green | Supply empty cart |
| Button 3 | Cancel | Red | Cancel current task |

### Task States

```
IDLE → QUEUED (button pressed, WCS notified)
     → ASSIGNED (WCS assigned AGV)
     → LOCKED (AGV picked up cart, cancel locked)
     → COMPLETED (task done)
```

### LED Indicators

| LED | Idle | Queued | Assigned | Error |
|-----|------|--------|----------|-------|
| Button 1/2 | Off | Blink slow | On | Blink fast |
| Tower Yellow | Off | - | On | Blink slow |
| Tower Red | Off | - | - | On (for errors) |

## Configuration

### WiFi Settings
Every unit uses the same firmware. On boot it scans the remembered networks and
tries the visible one with the strongest signal. A fresh unit starts with the
factory profile `AGV1` / `123456789`. The local access point is enabled
immediately and remains available while a phone is connected or configuring,
while background scans and reconnect attempts continue.

Connect a phone to:

- **SSID**: `CALLBOX-cb01` (the suffix is the current callbox ID)
- **Password**: `callbox123`
- **Configuration page**: `http://192.168.65.204/`

The lightweight page is embedded in the firmware (no CDN or external JavaScript).
Set a unique ID such as `cb01`, `cb02`, use **Scan WiFi** to select a network,
enter its password, and save. The selected network is moved to the highest-
priority slot; up to five profiles are stored in NVS. MQTT settings are saved at
the same time and applied without rebooting. Press **Finish** when setup is
complete; the AP remains available for recovery.

The same firmware binary can therefore be flashed to every callbox. Only the
saved ID, Wi-Fi and MQTT settings differ per unit.

### MQTT Settings
- **Broker**: hostname or IP address
- **Port**: normally `1883` or the plant-specific port such as `1884`
- **Username/password**: optional broker credentials

The current MQTT framing remains compatible with the existing WCS. Transport
and TLS migration is intentionally separate from the configuration portal.

## Building & Flashing

### Prerequisites
- ESP-IDF v6.1 installed
- Waveshare ESP32-S3-POE-ETH-8DI-8DO
- Python 3.7+

### Build
```bash
cd callbox_sews
idf.py build
```

### Flash to ESP32
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Configuration Menu
```bash
idf.py menuconfig
```

## Hardware Setup

### GPIO Mapping

| Component | GPIO | Function |
|-----------|------|----------|
| DI1 / Button 1 | GPIO4 | Input (active low) |
| DI2 / Button 2 | GPIO5 | Input (active low) |
| DI3 / Cancel | GPIO6 | Input (active low) |
| DI4..DI8 | GPIO7..11 | Reserved digital inputs |
| I2C SCL/SDA | GPIO41/GPIO42 | TCA9554PWR, address `0x20` |
| DO1 / Buzzer | TCA9554 P0 | Output (active low) |
| DO2..DO4 / Tower | TCA9554 P1..P3 | Red/yellow/green |
| DO5..DO7 / Button LEDs | TCA9554 P4..P6 | Button 1/2/cancel |
| DO8 | TCA9554 P7 | Reserved |
| W5500 SPI | GPIO12..16, GPIO39 | INT/MOSI/MISO/SCLK/CS/RESET |
| On-board buzzer | GPIO46 | BSP PWM output |

### Power Supply
- Input: 24VDC (for LEDs, tower light, buzzer)
- Buck Converter: 24VDC → 5V/3.3V for ESP32
- Protection: Polarity protection + fuse/resettable fuse

## Message Examples

### Button 1 Pressed (Call Task 1)
```json
{
  "type": "call",
  "task": 1,
  "seq": 1042,
  "ts": 1751791860
}
```

### WCS Response - AGV Assigned
```json
{
  "type": "assigned",
  "task": 1,
  "ref_seq": 1042,
  "agv_id": "agv03",
  "ts": 1751791865
}
```

### Heartbeat/Status (Retained)
```json
{
  "online": true,
  "task1": "assigned",
  "task2": "idle",
  "uptime": 86214,
  "fw": "1.0.0",
  "ts": 1751791920
}
```

## Troubleshooting

### Connection Issues
- Check WiFi SSID/password in config
- Verify MQTT broker IP and port
- Check firewall rules on broker

### LED Not Responding
- Verify GPIO configuration matches hardware
- Check 24V power supply to LEDs
- Verify transistor/relay circuits

### Button Not Detected
- Confirm GPIO pins match hardware
- Check pull-up resistor values
- Increase debounce time if needed

### MQTT Messages Not Received
- Subscribe to test topic: `mosquitto_sub -h <broker> -t "callbox/+/status"`
- Verify broker authentication
- Check QoS settings

## References

- [MQTT Protocol Specification](../Giao%20tiep%20MQTT%20TCP%20Bo%20goi%20WCS.docx)
- [Hardware Design Document](../Phuong_an_Bo_goi_AGV_SEW_20260706.docx)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/)

## License

Internal AUBOT Project - SEWS Manufacturing Facility
