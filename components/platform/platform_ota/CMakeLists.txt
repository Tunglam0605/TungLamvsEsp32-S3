idf_component_register(SRCS "platform_ota.c"
                            "platform_ota_idf.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES app_update esp_partition)