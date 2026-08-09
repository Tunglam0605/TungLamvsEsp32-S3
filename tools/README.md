# Tools

## mqtt_broker_test.py

Broker MQTT 3.1.1 tối giản để test LAN; không phải broker production và không cung cấp TLS/auth/persistence production.

```powershell
$py = 'C:\Espressif\tools\python\v6.1-dev\venv\Scripts\python.exe'
& $py -u .\tools\mqtt_broker_test.py --bind 0.0.0.0 --port 1883 --id 001
```

Cấu hình CallBox tới IP LAN máy chạy tool, port 1883, cùng ID. Tool hiển thị phím gửi lệnh WCS thử nghiệm. `__pycache__/` và `mqtt_background*.log` là artifact ignored.
