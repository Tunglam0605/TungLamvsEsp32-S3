idf_component_register(SRCS "platform_wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event freertos)
