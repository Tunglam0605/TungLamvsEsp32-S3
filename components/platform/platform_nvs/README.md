# platform_nvs

Wrapper generic cho ESP-IDF NVS. Cung cấp lifecycle init, handle open/close, typed get/set string/u8/u16/u32 và commit. Missing key được báo qua found/result để caller giữ default của mình.

Platform không biết namespace callbox, key Wi-Fi/MQTT hay migration. Schema/product persistence nằm ở CallBox callbox_config_store và sequence_store.
