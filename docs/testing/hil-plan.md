# Kế hoạch HIL — Waveshare ESP32-S3-POE-ETH-8DI-8DO

## Phạm vi và điều kiện chung

- Firmware HIL là đúng một application `board_hardware_test`, được `main` gọi.
- Dùng ESP-IDF v6.0.1 và profile kỳ vọng `ESP32-S3-WROOM-1U-N16R8`.
- Profile là giả định kiểm tra lúc chạy, không phải khẳng định cho mọi biến thể board.
- Không bật Wi-Fi, Ethernet, BLE, RS485, CAN, MQTT, RTC hoặc TF Card trong các stage này.
- Không xác nhận GPIO21 cho RS485 và TF Card đồng thời; xem `docs/hardware/open-issues.md`.
- `CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE` phải tắt cho Stage 1. Chỉ một stage DO có phê duyệt riêng mới được bật nó.

## Stage 1 — boot và safe state

Mục tiêu: chứng minh firmware boot ổn định, profile runtime, BSP, I2C/TCA9554, safe state, DI hiện thời, và lệnh RGB/buzzer hoạt động mà không chạy chuỗi DO.

1. Xác minh đúng cổng serial và cổng không bị chương trình khác giữ.
2. Chạy `idf.py set-target esp32s3`, `idf.py fullclean`, `idf.py build` trong shell đã export ESP-IDF v6.0.1.
3. Xác minh `sdkconfig` chứa `# CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE is not set`.
4. Flash và monitor bằng hai lệnh riêng trong ESP-IDF terminal:

   ```powershell
   idf.py -p COMx flash
   idf.py -p COMx monitor
   ```

5. Board hiện dùng không có nút RESET/EN vật lý. Để lấy lại boot log, giữ USB
   serial kết nối và power-cycle nguồn ngoài/PoE. Nếu USB là nguồn duy nhất,
   rút/cắm lại USB và chờ đúng COM port xuất hiện lại; không tự đổi sang cổng khác.
6. Dừng ngay nếu có lỗi BSP, I2C/TCA, safe-state, boot loop, panic, watchdog,
   hoặc profile làm firmware không boot.
7. Thoát monitor bằng `Ctrl + ]`, lưu boot log nguyên vẹn và cung cấp log để
   review. Không suy diễn trạng thái điện của DI/DO từ flash success hoặc log
   phần mềm đơn thuần.

HIL vật lý do người vận hành thực hiện trong ESP-IDF terminal. Automation và
review tài liệu không được tự mở COM port, flash lại hoặc chạy monitor.

## Stage 2 — DI và BOOT button

Mục tiêu: xác minh electrical mapping với tải/nguồn test đã cô lập.

1. Ghi lại raw DI mask và logical DI mask ban đầu.
2. Tác động từng DI một, ghi raw mask trước; chỉ sau đó mới kết luận polarity logical active.
3. Nhấn/nhả BOOT button và đối chiếu log. Khi chưa có quan sát này, diễn giải active-low của BOOT vẫn provisional.
4. Cập nhật pin-map chỉ khi có bằng chứng HIL truy nguyên được.

## Stage 3 — DO với tải an toàn

Chỉ thực hiện sau Stage 1/2 thành công, có phê duyệt riêng và tải cô lập.

1. Giữ `safe_mask`, xác nhận nguồn/tải và điểm đo trước khi tác động.
2. Bật có kiểm soát `CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE` hoặc dùng thao tác manual đã được review.
3. Đo từng DO bằng multimeter/logic analyser và xác minh reset/power-cycle trở về safe state.
4. Không đổi trạng thái `VERIFIED_HARDWARE_TEST` nếu không có phép đo hoặc quan sát vật lý.

## Console

Phase 0–1 không thêm interactive console. Firmware tự in trạng thái một lần khi boot và chỉ poll DI; thêm console sẽ mở rộng public API và bề mặt HIL. Các thao tác Stage 2/3 cần firmware/test procedure được review riêng.
