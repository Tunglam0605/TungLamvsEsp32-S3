idf_component_register(SRCS "callbox_sews.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos bsp esp_hw_support
                             esp_common platform_wifi callbox)
