# main

`main` chỉ là ESP-IDF entrypoint: `app_main() → callbox_app_run()`.

Nó chỉ require component `callbox`; không sở hữu Mission, MQTT, Wi‑Fi policy, portal, persistence hoặc BSP mapping. Logic mới phải nằm ở component phù hợp để tránh quay lại kiến trúc monolithic.
