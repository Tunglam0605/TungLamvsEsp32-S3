#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define BSP_DI_GPIO_FIRST GPIO_NUM_4
#define BSP_DI_GPIO_LAST GPIO_NUM_11

#define BSP_I2C_PORT I2C_NUM_0
#define BSP_I2C_SCL_GPIO GPIO_NUM_41
#define BSP_I2C_SDA_GPIO GPIO_NUM_42
#define BSP_I2C_GLITCH_IGNORE_COUNT 7

#define BSP_TCA9554_ADDRESS 0x20

#define BSP_RGB_GPIO GPIO_NUM_38
#define BSP_BUZZER_GPIO GPIO_NUM_46
#define BSP_BOOT_BUTTON_GPIO GPIO_NUM_0
