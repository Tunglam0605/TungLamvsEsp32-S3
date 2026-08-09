# Tools

tools/mqtt_broker_test.py là MQTT 3.1.1 broker test LAN cho firmware development. Nó hỗ trợ test interactive accepted, assigned, locked, completed, rejected, overdue và cancel_ack.

Nó không thay thế WCS/broker production: không có persistence, ACL, TLS production hoặc HA. Để test broker LAN, cấu hình CallBox trỏ broker IP máy test, port 1883 và logical CallBox ID tương ứng.

