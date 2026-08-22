#include "boot_validation.h"

#include "boot_validation_private.h"
#include "platform_ota.h"

static const boot_validation_ops_t s_default_ops = {
    .get_running_partition = platform_ota_get_running_partition,
    .get_partition_state = platform_ota_get_partition_state,
    .mark_running_valid = platform_ota_mark_running_valid,
    .mark_running_invalid_and_rollback_reboot =
        platform_ota_mark_running_invalid_and_rollback_reboot,
};
static const boot_validation_ops_t *s_ops = &s_default_ops;
static bool s_initialized;
static bool s_pending;
static boot_validation_lifecycle_t s_lifecycle = BOOT_VALIDATION_NOT_PENDING;

esp_err_t boot_validation_init(void)
{
    if (s_initialized) return ESP_OK;

    platform_ota_partition_t running = { 0 };
    platform_ota_image_state_t state = PLATFORM_OTA_IMG_UNDEFINED;
    esp_err_t err = s_ops->get_running_partition(&running);
    if (err != ESP_OK) return err;
    err = s_ops->get_partition_state(&running, &state);
    /* Factory and other non-OTA application partitions have no rollback
     * state; they are normal non-pending boots rather than an error. */
    if (err == ESP_ERR_NOT_SUPPORTED) err = ESP_OK;
    if (err != ESP_OK) return err;

    s_initialized = true;
    s_pending = state == PLATFORM_OTA_IMG_PENDING_VERIFY;
    s_lifecycle = s_pending ? BOOT_VALIDATION_PENDING : BOOT_VALIDATION_NOT_PENDING;
    return ESP_OK;
}

bool boot_validation_is_pending(void) { return s_initialized && s_pending; }
boot_validation_lifecycle_t boot_validation_get_lifecycle(void) { return s_lifecycle; }

esp_err_t boot_validation_mark_valid(void)
{
    if (!boot_validation_is_pending()) return ESP_OK;
    esp_err_t err = s_ops->mark_running_valid();
    if (err == ESP_OK) {
        s_pending = false;
        s_lifecycle = BOOT_VALIDATION_VALID;
    }
    return err;
}

esp_err_t boot_validation_request_rollback(void)
{
    if (!boot_validation_is_pending()) return ESP_OK;
    esp_err_t err = s_ops->mark_running_invalid_and_rollback_reboot();
    if (err == ESP_OK) s_lifecycle = BOOT_VALIDATION_ROLLBACK_REQUESTED;
    return err;
}

void boot_validation_test_set_ops(const boot_validation_ops_t *ops)
{
    s_ops = ops ? ops : &s_default_ops;
    boot_validation_test_reset();
}

void boot_validation_test_reset(void)
{
    s_initialized = false;
    s_pending = false;
    s_lifecycle = BOOT_VALIDATION_NOT_PENDING;
}
