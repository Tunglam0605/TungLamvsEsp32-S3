idf_component_register(SRCS "src/ota_service.c"
                            "src/ota_session.c"
                            "src/ota_validator.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES platform_ota
                       PRIV_REQUIRES app_update bootloader_support esp_app_format)