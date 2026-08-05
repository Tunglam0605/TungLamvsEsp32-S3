#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_buzzer;
    bool has_rgb;
    uint8_t digital_input_count;
    uint8_t digital_output_count;
} bsp_capabilities_t;

typedef enum {
    BSP_DI_1 = 0,
    BSP_DI_2,
    BSP_DI_3,
    BSP_DI_4,
    BSP_DI_5,
    BSP_DI_6,
    BSP_DI_7,
    BSP_DI_8,
    BSP_DI_COUNT,
} bsp_di_channel_t;

typedef enum {
    BSP_DO_1 = 0,
    BSP_DO_2,
    BSP_DO_3,
    BSP_DO_4,
    BSP_DO_5,
    BSP_DO_6,
    BSP_DO_7,
    BSP_DO_8,
    BSP_DO_COUNT,
} bsp_do_channel_t;

esp_err_t bsp_board_init(void);
const bsp_capabilities_t *bsp_board_get_capabilities(void);

esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_bus(void);

esp_err_t bsp_di_init(void);
bool bsp_di_read_raw(bsp_di_channel_t channel);
uint8_t bsp_di_read_raw_mask(void);
bool bsp_di_read(bsp_di_channel_t channel);
uint8_t bsp_di_read_mask(void);
bool bsp_di_uses_provisional_active_low(void);

esp_err_t bsp_do_init(void);
esp_err_t bsp_do_write(bsp_do_channel_t channel, bool state);
esp_err_t bsp_do_write_mask(uint8_t mask);
uint8_t bsp_do_get_desired_mask(void);
uint8_t bsp_do_get_applied_mask(void);
uint8_t bsp_do_get_safe_mask(void);
esp_err_t bsp_do_apply_safe_state(void);
bool bsp_do_uses_provisional_active_high(void);

esp_err_t bsp_buzzer_init(void);
esp_err_t bsp_buzzer_set(bool enabled);

esp_err_t bsp_rgb_init(void);
esp_err_t bsp_rgb_set(uint8_t red, uint8_t green, uint8_t blue);

esp_err_t bsp_boot_button_init(void);
bool bsp_boot_button_is_pressed(void);

#ifdef __cplusplus
}
#endif
