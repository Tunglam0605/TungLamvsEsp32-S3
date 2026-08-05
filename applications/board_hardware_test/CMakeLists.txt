idf_component_register(SRCS "board_hardware_test.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bsp_waveshare_s3_8di8do esp_psram spi_flash freertos)
