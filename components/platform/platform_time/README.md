# platform_time

Provider thời gian/SNTP ESP-IDF. Nó không chọn business policy hay MQTT transaction logic.

CallBox time_sync chọn primary/fallback từ Config_t, chạy cho TCP lẫn TLS. Timestamp protocol là Unix-style telemetry; seq/ref_seq mới là cơ chế correlation. TLS chỉ bắt đầu sau khi provider báo thời gian hợp lệ.

