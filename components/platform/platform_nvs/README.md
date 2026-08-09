# platform_nvs

Provider primitive cho NVS ESP-IDF. Nó cung cấp open/read/write/commit semantics nhưng không đặt namespace/key product hoặc migrate schema.

CallBox persistence modules sở hữu namespace callbox, Config_t keys và seq_num. Tách này giữ Platform tái dùng được và tránh NVS schema chảy xuống provider.

