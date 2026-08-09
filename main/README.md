# main — composition entrypoint

main chỉ chứa ESP-IDF entrypoint:

    app_main() → callbox_app_run()

Nó require component callbox và không là nơi đặt logic sản phẩm. Giữ entrypoint mỏng giúp firmware vẫn chia lớp, test/audit dependency rõ và tránh quay lại kiến trúc monolithic.

main không sở hữu:

- Mission state, sequence, retry hoặc button policy;
- MQTT, Wi-Fi product policy, portal hoặc NVS schema;
- mapping phần cứng, BSP hoặc driver.

Khi cần thêm chức năng, đặt vào CallBox, BSP hoặc Platform theo responsibility tương ứng; không thêm xử lý nghiệp vụ vào app_main.

