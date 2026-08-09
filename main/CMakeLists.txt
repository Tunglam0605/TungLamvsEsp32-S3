idf_component_register(SRCS "callbox_sews.c"
                             "config_portal.c"
                             "network_status_task.c"
                             "state_machine.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "../img/company-logo-transparent.png"
                    REQUIRES freertos esp_netif esp_timer
                             bsp esp_http_server lwip
                             platform_wifi callbox)
