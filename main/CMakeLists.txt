idf_component_register(SRCS "gateway_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES gateway nvs_flash esp_event esp_netif)
