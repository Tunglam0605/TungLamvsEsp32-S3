if(CONFIG_CALLBOX_PRODUCTION_BUILD)
    if(NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
        message(FATAL_ERROR "Production Callbox build requires bootloader application rollback")
    endif()
    if(NOT CONFIG_SECURE_BOOT OR NOT CONFIG_SECURE_BOOT_V2_ENABLED)
        message(FATAL_ERROR "Production Callbox build requires Secure Boot V2")
    endif()
    if(NOT CONFIG_SECURE_FLASH_ENC_ENABLED OR
       NOT CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE)
        message(FATAL_ERROR "Production Callbox build requires flash encryption in release mode")
    endif()
    if(NOT CONFIG_NVS_ENCRYPTION OR
       NOT CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC)
        message(FATAL_ERROR "Production Callbox build requires NVS encryption with HMAC key protection")
    endif()
endif()

idf_component_register(SRCS "src/protocol_types.c"
                             "src/app_event_queue.c"
                             "src/health_monitor.c"
                             "src/ota_boot_validator.c"
                             "src/ota_policy.c"
                             "src/ota_output_adapter.c"
                             "src/ota_web_handler.c"
                             "src/status.c"
                             "src/button_gate.c"
                             "src/callbox_config_store.c"
                             "src/sequence_service.c"
                             "src/sequence_store.c"
                             "src/nvs_storage.c"
                             "src/callbox_storage_migration.c"
                             "src/callbox_io.c"
                             "src/io_handler.c"
                             "src/io_debug.c"
                             "src/led_control.c"
                             "src/output_renderer.c"
                             "src/wifi_init.c"
                             "src/network_link.c"
                             "src/mqtt_client.c"
                             "src/time_sync.c"
                             "src/state_machine.c"
                             "src/config_portal.c"
                             "src/network_status_task.c"
                             "src/callbox_app.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    EMBED_FILES "company-logo-transparent.png"
                    REQUIRES freertos platform_nvs bsp esp_timer platform_wifi
                             mqtt mbedtls platform_time esp_http_server
                             esp_netif lwip esp_system esp_common esp_hw_support
                             bootloader_support boot_validation ota)
