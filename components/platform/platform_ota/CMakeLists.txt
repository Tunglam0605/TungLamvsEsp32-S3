idf_component_register(SRCS "platform_ota.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_update esp_https_ota esp_http_client esp_partition esp_netif esp_timer freertos)
