#include "platform_ota_internal.h"

static const esp_partition_t *idf_get_running_partition(void)
{
    return esp_ota_get_running_partition();
}

static const esp_partition_t *idf_get_next_update_partition(const esp_partition_t *start_from)
{
    return esp_ota_get_next_update_partition(start_from);
}

static esp_err_t idf_begin(const esp_partition_t *partition, size_t image_size,
                           esp_ota_handle_t *out_handle)
{
    return esp_ota_begin(partition, image_size, out_handle);
}

static esp_err_t idf_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    return esp_ota_write(handle, data, size);
}

static esp_err_t idf_end(esp_ota_handle_t handle)
{
    return esp_ota_end(handle);
}

static esp_err_t idf_abort(esp_ota_handle_t handle)
{
    return esp_ota_abort(handle);
}

static esp_err_t idf_set_boot_partition(const esp_partition_t *partition)
{
    return esp_ota_set_boot_partition(partition);
}

static esp_err_t idf_get_state_partition(const esp_partition_t *partition,
                                         esp_ota_img_states_t *state)
{
    return esp_ota_get_state_partition(partition, state);
}

static esp_err_t idf_mark_app_valid_cancel_rollback(void)
{
    return esp_ota_mark_app_valid_cancel_rollback();
}

static esp_err_t idf_mark_app_invalid_rollback_and_reboot(void)
{
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

const platform_ota_ops_t g_platform_ota_idf_ops = {
    .get_running_partition = idf_get_running_partition,
    .get_next_update_partition = idf_get_next_update_partition,
    .begin = idf_begin,
    .write = idf_write,
    .end = idf_end,
    .abort = idf_abort,
    .set_boot_partition = idf_set_boot_partition,
    .get_state_partition = idf_get_state_partition,
    .mark_app_valid_cancel_rollback = idf_mark_app_valid_cancel_rollback,
    .mark_app_invalid_rollback_and_reboot = idf_mark_app_invalid_rollback_and_reboot,
};