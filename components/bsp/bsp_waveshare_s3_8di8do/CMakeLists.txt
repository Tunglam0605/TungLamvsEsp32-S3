idf_component_register(
    SRCS
        "src/bsp_board.c"
        "src/bsp_i2c.c"
        "src/bsp_di.c"
        "src/bsp_do_state.c"
        "src/bsp_do.c"
        "src/bsp_buzzer.c"
        "src/bsp_rgb.c"
        "src/bsp_boot_button.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES drv_tca9554 esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt freertos)
