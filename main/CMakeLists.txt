idf_component_register(SRCS "esp32_s3_base.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp nvs_flash esp_event esp_netif)
