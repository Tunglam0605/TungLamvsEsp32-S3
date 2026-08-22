idf_component_register(SRCS "test_ota_service.c"
                       PRIV_INCLUDE_DIRS "../private_include"
                       REQUIRES unity ota app_update esp_app_format
                       WHOLE_ARCHIVE)