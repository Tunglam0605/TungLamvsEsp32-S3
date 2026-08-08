idf_component_register(
    SRCS "src/bsp_board.c"
         "src/bsp_buzzer.c"
         "src/bsp_di.c"
         "src/bsp_do.c"
         "src/bsp_eth.c"
         "src/bsp_i2c.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES tca9554 esp_event esp_eth esp_netif esp_driver_gpio
             esp_driver_i2c esp_driver_ledc esp_driver_spi
)
