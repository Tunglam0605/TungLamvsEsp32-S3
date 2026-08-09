idf_component_register(SRCS "callbox_sews.c"
                             "config_portal.c"
                             "mqtt_client.c"
                             "network_status_task.c"
                             "state_machine.c"
                             "time_sync.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "../img/company-logo-transparent.png"
                    REQUIRES freertos esp_netif esp_event esp_timer
                             bsp esp_http_server mqtt mbedtls lwip platform_time
                             platform_wifi callbox)
