# HIL checklist — Waveshare ESP32-S3-POE-ETH-8DI-8DO

## Chuẩn bị

- [ ] Ghi board label/revision/module marking; nếu không đọc được, ghi rõ `unidentified`.
- [ ] Chỉ ra cổng serial vật lý đã xác minh; không tự đổi cổng nếu cổng yêu cầu đang bận.
- [ ] Đóng Serial Monitor, Arduino IDE, VS Code monitor hoặc terminal khác đang giữ cổng.
- [ ] Xác nhận ESP-IDF v6.0.1 và target `esp32s3`.
- [ ] Xác nhận `# CONFIG_PLATFORM_HARDWARE_TEST_RUN_OUTPUT_SEQUENCE is not set` trong `sdkconfig`.
- [ ] Kiểm tra tải DO đã cô lập; Stage 1 không được chạy DO sequence.

## Stage 1

- [ ] `idf.py set-target esp32s3`
- [ ] `idf.py fullclean`
- [ ] `idf.py build`
- [ ] `idf.py -p <verified-port> flash`
- [ ] `idf.py -p <verified-port> monitor`
- [ ] Power-cycle nguồn ngoài/PoE trong khi giữ USB serial, hoặc rút/cắm lại
      USB nếu USB là nguồn duy nhất. Board không có nút RESET/EN vật lý.
- [ ] Nếu COM port tạm biến mất, chờ đúng cổng trở lại; không đổi sang cổng khác.
- [ ] Log ESP-IDF version, chip revision và reset reason.
- [ ] Log Flash/PSRAM detected và so sánh profile N16R8.
- [ ] Log partition table.
- [ ] Log BSP init, I2C init, TCA9554 address `0x20`, và safe-state result.
- [ ] Log DO `desired_mask`, `applied_mask`, `safe_mask`, `applied_valid`.
- [ ] Log DI raw/logical mask; polarity chỉ là provisional cho tới Stage 2.
- [ ] Log BOOT button state; thực hiện press/release ở Stage 2 nếu cần xác minh physical.
- [ ] Quan sát RGB R/G/B/off và buzzer; ghi rõ nếu chỉ có command success, chưa quan sát được.
- [ ] Dừng HIL nếu TCA/I2C/BSP/safe state fail, boot loop, panic hoặc watchdog.
- [ ] Thoát monitor bằng `Ctrl + ]` sau khi boot ổn định.

## Bằng chứng và trạng thái

- [ ] Sao chép mẫu trong `docs/testing/hil-results/README.md` thành
      `HIL-YYYYMMDD-board-<identifier>.md` và điền bằng chứng thực tế.
- [ ] Lưu boot log liên quan cùng ngày, board identifier và HEAD commit.
- [ ] Ghi build, flash, monitor, observed/unobserved physical results và warning/error.
- [ ] Không dùng flash success làm bằng chứng `VERIFIED_HARDWARE_TEST`.
- [ ] Chỉ cập nhật pin-map/open issues khi bằng chứng phần cứng tương ứng đã được ghi lại.
