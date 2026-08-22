#include "platform_ota.h"

#include <limits.h>
#include <string.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "platform_ota_internal.h"

#define PLATFORM_OTA_SESSION_MAGIC 0x504F5441U

static const platform_ota_ops_t *s_ops = &g_platform_ota_idf_ops;

static bool is_ota_app_partition(const esp_partition_t *partition)
{
    return partition != NULL &&
           partition->type == ESP_PARTITION_TYPE_APP &&
           partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
           partition->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
}

static bool same_partition(const esp_partition_t *a, const esp_partition_t *b)
{
    if (a == NULL || b == NULL) return false;
    return a == b ||
           (a->address == b->address && a->size == b->size &&
            a->type == b->type && a->subtype == b->subtype);
}

static platform_ota_slot_t map_slot(const esp_partition_t *partition)
{
    if (partition == NULL || partition->type != ESP_PARTITION_TYPE_APP) {
        return PLATFORM_OTA_SLOT_UNKNOWN;
    }

    switch (partition->subtype) {
    case ESP_PARTITION_SUBTYPE_APP_FACTORY:
        return PLATFORM_OTA_SLOT_FACTORY;
    case ESP_PARTITION_SUBTYPE_APP_OTA_0:
        return PLATFORM_OTA_SLOT_0;
    case ESP_PARTITION_SUBTYPE_APP_OTA_1:
        return PLATFORM_OTA_SLOT_1;
    default:
        return is_ota_app_partition(partition) ? PLATFORM_OTA_SLOT_OTHER
                                               : PLATFORM_OTA_SLOT_UNKNOWN;
    }
}

static esp_err_t fill_partition(const esp_partition_t *native,
                                platform_ota_partition_t *out_partition)
{
    if (native == NULL || out_partition == NULL) return ESP_ERR_INVALID_ARG;

    memset(out_partition, 0, sizeof(*out_partition));
    (void)strncpy(out_partition->label, native->label,
                  PLATFORM_OTA_PARTITION_LABEL_MAX);
    out_partition->label[PLATFORM_OTA_PARTITION_LABEL_MAX] = '\0';
    out_partition->address = native->address;
    out_partition->size = native->size;
    out_partition->slot = map_slot(native);
    out_partition->_native = native;
    return ESP_OK;
}

static const esp_partition_t *native_partition(const platform_ota_partition_t *partition)
{
    return partition == NULL ? NULL : (const esp_partition_t *)partition->_native;
}

static esp_err_t validate_inactive_ota_target(const platform_ota_partition_t *target,
                                              const esp_partition_t **out_native)
{
    if (target == NULL || out_native == NULL) return ESP_ERR_INVALID_ARG;

    const esp_partition_t *native = native_partition(target);
    if (!is_ota_app_partition(native)) return ESP_ERR_INVALID_ARG;

    const esp_partition_t *running = s_ops->get_running_partition();
    if (running == NULL) return ESP_ERR_NOT_FOUND;
    if (same_partition(native, running)) return ESP_ERR_OTA_PARTITION_CONFLICT;

    *out_native = native;
    return ESP_OK;
}

static bool session_ready(const platform_ota_session_t *session)
{
    return session != NULL && session->_magic == PLATFORM_OTA_SESSION_MAGIC;
}

static void close_session(platform_ota_session_t *session)
{
    if (session == NULL) return;
    session->_handle = 0;
    session->_target_native = NULL;
    session->_target_size = 0;
    session->_expected_size = 0;
    session->_bytes_written = 0;
    session->_active = 0;
    session->_magic = PLATFORM_OTA_SESSION_MAGIC;
}

void platform_ota_session_init(platform_ota_session_t *session)
{
    if (session == NULL) return;
    memset(session, 0, sizeof(*session));
    session->_magic = PLATFORM_OTA_SESSION_MAGIC;
}

esp_err_t platform_ota_get_running_partition(platform_ota_partition_t *out_partition)
{
    if (out_partition == NULL) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *running = s_ops->get_running_partition();
    return running == NULL ? ESP_ERR_NOT_FOUND : fill_partition(running, out_partition);
}

esp_err_t platform_ota_get_next_update_partition(platform_ota_partition_t *out_partition)
{
    if (out_partition == NULL) return ESP_ERR_INVALID_ARG;

    const esp_partition_t *running = s_ops->get_running_partition();
    if (running == NULL) return ESP_ERR_NOT_FOUND;

    const esp_partition_t *next = s_ops->get_next_update_partition(NULL);
    if (next == NULL) return ESP_ERR_NOT_FOUND;
    if (!is_ota_app_partition(next)) return ESP_ERR_INVALID_STATE;
    if (same_partition(next, running)) return ESP_ERR_OTA_PARTITION_CONFLICT;

    return fill_partition(next, out_partition);
}

esp_err_t platform_ota_session_begin(platform_ota_session_t *session,
                                     const platform_ota_partition_t *target,
                                     size_t image_size)
{
    if (!session_ready(session) || target == NULL) return ESP_ERR_INVALID_ARG;
    if (session->_active != 0U) return ESP_ERR_INVALID_STATE;
    if (image_size == 0U) return ESP_ERR_INVALID_SIZE;

    const esp_partition_t *native = NULL;
    esp_err_t err = validate_inactive_ota_target(target, &native);
    if (err != ESP_OK) return err;

    if (image_size != PLATFORM_OTA_IMAGE_SIZE_UNKNOWN && image_size > native->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    const size_t idf_image_size = image_size == PLATFORM_OTA_IMAGE_SIZE_UNKNOWN
                                      ? OTA_WITH_SEQUENTIAL_WRITES
                                      : image_size;
    err = s_ops->begin(native, idf_image_size, &handle);
    if (err != ESP_OK) return err;

    session->_handle = (uintptr_t)handle;
    session->_target_native = native;
    session->_target_size = native->size;
    session->_expected_size = image_size;
    session->_bytes_written = 0;
    session->_active = 1U;
    return ESP_OK;
}

esp_err_t platform_ota_session_write(platform_ota_session_t *session,
                                     const void *data,
                                     size_t size)
{
    if (!session_ready(session)) return ESP_ERR_INVALID_ARG;
    if (session->_active == 0U) return ESP_ERR_INVALID_STATE;
    if (size == 0U) return ESP_OK;
    if (data == NULL) return ESP_ERR_INVALID_ARG;

    if (session->_bytes_written > session->_target_size ||
        size > session->_target_size - session->_bytes_written) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (session->_expected_size != PLATFORM_OTA_IMAGE_SIZE_UNKNOWN &&
        (session->_bytes_written > session->_expected_size ||
         size > session->_expected_size - session->_bytes_written)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = s_ops->write((esp_ota_handle_t)session->_handle, data, size);
    if (err == ESP_OK) session->_bytes_written += size;
    return err;
}

esp_err_t platform_ota_session_finish(platform_ota_session_t *session)
{
    if (!session_ready(session)) return ESP_ERR_INVALID_ARG;
    if (session->_active == 0U) return ESP_ERR_INVALID_STATE;
    if (session->_bytes_written == 0U) return ESP_ERR_INVALID_STATE;

    if (session->_expected_size != PLATFORM_OTA_IMAGE_SIZE_UNKNOWN &&
        session->_bytes_written != session->_expected_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_ota_handle_t handle = (esp_ota_handle_t)session->_handle;
    esp_err_t err = s_ops->end(handle);
    close_session(session);
    return err;
}

esp_err_t platform_ota_session_abort(platform_ota_session_t *session)
{
    if (!session_ready(session)) return ESP_ERR_INVALID_ARG;
    if (session->_active == 0U) return ESP_ERR_INVALID_STATE;

    const esp_ota_handle_t handle = (esp_ota_handle_t)session->_handle;
    esp_err_t err = s_ops->abort(handle);
    close_session(session);
    return err;
}

esp_err_t platform_ota_set_boot_partition(const platform_ota_partition_t *target)
{
    const esp_partition_t *native = NULL;
    esp_err_t err = validate_inactive_ota_target(target, &native);
    if (err != ESP_OK) return err;
    return s_ops->set_boot_partition(native);
}

esp_err_t platform_ota_get_partition_state(const platform_ota_partition_t *partition,
                                           platform_ota_image_state_t *out_state)
{
    if (partition == NULL || out_state == NULL) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *native = native_partition(partition);
    if (!is_ota_app_partition(native)) return ESP_ERR_NOT_SUPPORTED;

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = s_ops->get_state_partition(native, &state);
    if (err != ESP_OK) return err;

    switch (state) {
    case ESP_OTA_IMG_NEW:
        *out_state = PLATFORM_OTA_IMG_NEW;
        break;
    case ESP_OTA_IMG_PENDING_VERIFY:
        *out_state = PLATFORM_OTA_IMG_PENDING_VERIFY;
        break;
    case ESP_OTA_IMG_VALID:
        *out_state = PLATFORM_OTA_IMG_VALID;
        break;
    case ESP_OTA_IMG_INVALID:
        *out_state = PLATFORM_OTA_IMG_INVALID;
        break;
    case ESP_OTA_IMG_ABORTED:
        *out_state = PLATFORM_OTA_IMG_ABORTED;
        break;
    case ESP_OTA_IMG_UNDEFINED:
    default:
        *out_state = PLATFORM_OTA_IMG_UNDEFINED;
        break;
    }
    return ESP_OK;
}

esp_err_t platform_ota_mark_running_valid(void)
{
    return s_ops->mark_app_valid_cancel_rollback();
}

esp_err_t platform_ota_mark_running_invalid_and_rollback_reboot(void)
{
    return s_ops->mark_app_invalid_rollback_and_reboot();
}

bool platform_ota_session_is_active(const platform_ota_session_t *session)
{
    return session_ready(session) && session->_active != 0U;
}

size_t platform_ota_session_bytes_written(const platform_ota_session_t *session)
{
    return session_ready(session) ? session->_bytes_written : 0U;
}

void platform_ota_test_set_ops(const platform_ota_ops_t *ops)
{
    s_ops = ops != NULL ? ops : &g_platform_ota_idf_ops;
}

void platform_ota_test_reset_ops(void)
{
    s_ops = &g_platform_ota_idf_ops;
}