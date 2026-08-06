idf_component_register(SRCS "src/tca9554.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES esp_driver_i2c freertos)
