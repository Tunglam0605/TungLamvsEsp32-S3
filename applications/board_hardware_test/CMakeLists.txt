idf_component_register(SRCS "board_hardware_test.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bsp_waveshare_s3_8di8do esp_app_format esp_partition esp_psram esp_system spi_flash freertos)
