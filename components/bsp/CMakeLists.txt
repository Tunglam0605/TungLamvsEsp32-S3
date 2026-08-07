idf_component_register(
    SRCS "src/bsp_board.c"
         "src/bsp_buzzer.c"
         "src/bsp_di.c"
         "src/bsp_do.c"
         "src/bsp_eth.c"
         "src/bsp_expander.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_eth esp_netif esp_driver_gpio esp_driver_i2c
             esp_driver_ledc esp_driver_spi
)
