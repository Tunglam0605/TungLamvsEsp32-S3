#include "bsp_waveshare_s3_8di8do.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "tca9554.h"

#include "bsp_do_state.h"
#include "bsp_pins.h"

static const char *TAG = "bsp_do";
static const uint8_t BSP_DO_LOGICAL_SAFE_MASK = 0x00;

static tca9554_handle_t s_expander;
static SemaphoreHandle_t s_lock;
static bsp_do_state_t s_state;
static bool s_initialized;

static esp_err_t bsp_do_apply_mask_locked(uint8_t logical_mask)
{
    bsp_do_state_set_desired(&s_state, logical_mask);
    const uint8_t register_mask = bsp_do_state_to_register_mask(&s_state, logical_mask);
    const esp_err_t err = tca9554_write_outputs(s_expander, register_mask);
    if (err == ESP_OK) {
        bsp_do_state_commit_applied(&s_state, logical_mask);
    } else {
        ESP_LOGE(TAG, "Output write failed; desired=0x%02x applied=0x%02x err=%s",
                 s_state.desired_mask,
                 s_state.applied_mask,
                 esp_err_to_name(err));
    }
    return err;
}

static esp_err_t bsp_do_take_lock(void)
{
    if (!s_initialized || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t bsp_do_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    const tca9554_config_t config = {
        .bus = bsp_i2c_get_bus(),
        .address = BSP_TCA9554_ADDRESS,
        .timeout_ms = 1000,
    };
    err = tca9554_create(&config, &s_expander);
    if (err != ESP_OK) {
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        (void)tca9554_delete(s_expander);
        s_expander = NULL;
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_PLATFORM_DO_ACTIVE_HIGH_PROVISIONAL
    const bool logical_on_is_register_high = true;
#else
    const bool logical_on_is_register_high = false;
#endif
    bsp_do_state_init(&s_state, logical_on_is_register_high, BSP_DO_LOGICAL_SAFE_MASK);

    // Latch a safe physical value before enabling the expander output drivers.
    const uint8_t safe_register_mask = bsp_do_state_to_register_mask(&s_state, s_state.safe_mask);
    err = tca9554_write_outputs(s_expander, safe_register_mask);
    if (err == ESP_OK) {
        err = tca9554_set_direction(s_expander, 0x00);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply early safe state: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        (void)tca9554_delete(s_expander);
        s_expander = NULL;
        return err;
    }

    bsp_do_state_commit_applied(&s_state, s_state.safe_mask);
    s_initialized = true;
    ESP_LOGI(TAG, "Safe logical DO mask applied: 0x%02x (provisional register polarity: %s)",
             s_state.safe_mask,
             logical_on_is_register_high ? "active-high" : "active-low");
    return ESP_OK;
}

esp_err_t bsp_do_write(bsp_do_channel_t channel, bool state)
{
    if (channel < BSP_DO_1 || channel >= BSP_DO_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bsp_do_take_lock();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t requested_mask = s_state.desired_mask;
    const uint8_t channel_mask = (uint8_t)(1U << channel);
    if (state) {
        requested_mask |= channel_mask;
    } else {
        requested_mask &= (uint8_t)~channel_mask;
    }
    err = bsp_do_apply_mask_locked(requested_mask);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t bsp_do_write_mask(uint8_t mask)
{
    esp_err_t err = bsp_do_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = bsp_do_apply_mask_locked(mask);
    xSemaphoreGive(s_lock);
    return err;
}

uint8_t bsp_do_get_desired_mask(void)
{
    if (bsp_do_take_lock() != ESP_OK) {
        return 0;
    }
    const uint8_t value = s_state.desired_mask;
    xSemaphoreGive(s_lock);
    return value;
}

uint8_t bsp_do_get_applied_mask(void)
{
    if (bsp_do_take_lock() != ESP_OK) {
        return 0;
    }
    const uint8_t value = s_state.applied_mask;
    xSemaphoreGive(s_lock);
    return value;
}

uint8_t bsp_do_get_safe_mask(void)
{
    if (bsp_do_take_lock() != ESP_OK) {
        return BSP_DO_LOGICAL_SAFE_MASK;
    }
    const uint8_t value = s_state.safe_mask;
    xSemaphoreGive(s_lock);
    return value;
}

esp_err_t bsp_do_apply_safe_state(void)
{
    esp_err_t err = bsp_do_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = bsp_do_apply_mask_locked(s_state.safe_mask);
    xSemaphoreGive(s_lock);
    return err;
}

bool bsp_do_uses_provisional_active_high(void)
{
#if CONFIG_PLATFORM_DO_ACTIVE_HIGH_PROVISIONAL
    return true;
#else
    return false;
#endif
}
