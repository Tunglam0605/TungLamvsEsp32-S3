#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ota_service_private.h"

#define OTA_EVENT_BATCH_CAPACITY 8U

typedef struct {
    ota_status_t status;
    platform_ota_session_t session;
    uint8_t prefix[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)];
    size_t prefix_size;
    ota_event_callback_t callback;
    void *callback_context;
    bool initialized;
} ota_service_context_t;

typedef struct {
    ota_event_t events[OTA_EVENT_BATCH_CAPACITY];
    size_t count;
    ota_event_callback_t callback;
    void *callback_context;
} ota_event_batch_t;

static esp_err_t default_get_next(platform_ota_partition_t *target)
{
    return platform_ota_get_next_update_partition(target);
}

static esp_err_t default_begin(platform_ota_session_t *session,
                               const platform_ota_partition_t *target,
                               size_t size)
{
    return platform_ota_session_begin(session, target, size);
}

static esp_err_t default_write(platform_ota_session_t *session, const void *data, size_t size)
{
    return platform_ota_session_write(session, data, size);
}

static esp_err_t default_finish(platform_ota_session_t *session)
{
    return platform_ota_session_finish(session);
}

static esp_err_t default_abort(platform_ota_session_t *session)
{
    return platform_ota_session_abort(session);
}

static bool default_active(const platform_ota_session_t *session)
{
    return platform_ota_session_is_active(session);
}

static esp_err_t default_set_boot(const platform_ota_partition_t *target)
{
    return platform_ota_set_boot_partition(target);
}

static const esp_app_desc_t *default_description(void)
{
    return esp_app_get_description();
}

static const ota_service_ops_t s_default_ops = {
    .get_next_update_partition = default_get_next,
    .session_begin = default_begin,
    .session_write = default_write,
    .session_finish = default_finish,
    .session_abort = default_abort,
    .session_is_active = default_active,
    .set_boot_partition = default_set_boot,
    .get_running_description = default_description,
};

static const ota_service_ops_t *s_ops = &s_default_ops;
static ota_service_context_t s_context;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

static void lock_service(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    }
    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock_service(void)
{
    (void)xSemaphoreGive(s_mutex);
}

static void snapshot_locked(void)
{
    s_context.status.platform_session_active = s_ops->session_is_active(&s_context.session);
}

static void event_batch_begin_locked(ota_event_batch_t *batch)
{
    memset(batch, 0, sizeof(*batch));
    batch->callback = s_context.callback;
    batch->callback_context = s_context.callback_context;
}

static void event_queue_locked(ota_event_batch_t *batch, ota_event_type_t type)
{
    snapshot_locked();
    if (batch->callback == NULL || batch->count >= OTA_EVENT_BATCH_CAPACITY) {
        return;
    }
    batch->events[batch->count].type = type;
    batch->events[batch->count].status = s_context.status;
    batch->count++;
}

static void event_batch_dispatch(const ota_event_batch_t *batch)
{
    if (batch->callback == NULL) {
        return;
    }
    for (size_t i = 0; i < batch->count; ++i) {
        batch->callback(&batch->events[i], batch->callback_context);
    }
}

static void clear_transaction_locked(void)
{
    const ota_event_callback_t callback = s_context.callback;
    void *const callback_context = s_context.callback_context;
    const bool initialized = s_context.initialized;

    memset(&s_context, 0, sizeof(s_context));
    s_context.callback = callback;
    s_context.callback_context = callback_context;
    s_context.initialized = initialized;
    platform_ota_session_init(&s_context.session);
    s_context.status.state = OTA_STATE_IDLE;
}

static void transition_locked(ota_state_t state, ota_event_batch_t *batch)
{
    s_context.status.state = state;
    event_queue_locked(batch, OTA_EVENT_STATE_CHANGED);
}

static esp_err_t fail_locked(esp_err_t error, ota_event_batch_t *batch)
{
    if (s_ops->session_is_active(&s_context.session)) {
        (void)s_ops->session_abort(&s_context.session);
    }
    s_context.status.last_error = error;
    s_context.status.has_staged_image = false;
    transition_locked(OTA_STATE_FAILED, batch);
    event_queue_locked(batch, OTA_EVENT_FAILED);
    return error;
}

esp_err_t ota_service_init(void)
{
    esp_err_t result = ESP_OK;
    lock_service();
    if (!s_context.initialized) {
        clear_transaction_locked();
        s_context.initialized = true;
    } else if (s_context.status.state != OTA_STATE_IDLE ||
               s_ops->session_is_active(&s_context.session)) {
        result = ESP_ERR_INVALID_STATE;
    }
    unlock_service();
    return result;
}

esp_err_t ota_service_reset(void)
{
    ota_event_batch_t events;
    esp_err_t abort_result = ESP_OK;

    lock_service();
    event_batch_begin_locked(&events);

    if (!s_context.initialized) {
        clear_transaction_locked();
        s_context.initialized = true;
        unlock_service();
        return ESP_OK;
    }

    if (s_context.status.state == OTA_STATE_INSTALLING ||
        s_context.status.state == OTA_STATE_REBOOT_PENDING) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }

    const bool state_changed = s_context.status.state != OTA_STATE_IDLE;
    if (s_ops->session_is_active(&s_context.session)) {
        abort_result = s_ops->session_abort(&s_context.session);
    }
    clear_transaction_locked();
    if (state_changed) {
        event_queue_locked(&events, OTA_EVENT_STATE_CHANGED);
    }
    unlock_service();
    event_batch_dispatch(&events);
    return abort_result;
}

esp_err_t ota_service_begin(size_t expected_image_size)
{
    ota_event_batch_t events;

    lock_service();
    event_batch_begin_locked(&events);
    if (!s_context.initialized) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }
    if (expected_image_size == 0U) {
        unlock_service();
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_context.status.state != OTA_STATE_IDLE ||
        s_ops->session_is_active(&s_context.session)) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }

    clear_transaction_locked();
    s_context.status.expected_size = expected_image_size;
    transition_locked(OTA_STATE_ADMISSION, &events);
    unlock_service();
    event_batch_dispatch(&events);
    return ESP_OK;
}

esp_err_t ota_service_write(const void *data, size_t size)
{
    ota_event_batch_t events;
    esp_err_t result = ESP_OK;

    lock_service();
    event_batch_begin_locked(&events);
    if (s_context.status.state != OTA_STATE_ADMISSION &&
        s_context.status.state != OTA_STATE_RECEIVING) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }
    if (size == 0U) {
        unlock_service();
        return ESP_OK;
    }
    if (data == NULL) {
        unlock_service();
        return ESP_ERR_INVALID_ARG;
    }

    result = ota_session_check_chunk_bounds(s_context.status.expected_size,
                                            s_context.status.bytes_received,
                                            size);
    if (result != ESP_OK) {
        result = fail_locked(result, &events);
        unlock_service();
        event_batch_dispatch(&events);
        return result;
    }

    const uint8_t *input = (const uint8_t *)data;
    size_t remaining = size;

    if (s_context.status.state == OTA_STATE_ADMISSION) {
        const size_t prefix_required = ota_validator_prefix_size();
        const size_t wanted = prefix_required - s_context.prefix_size;
        const size_t taken = remaining < wanted ? remaining : wanted;

        memcpy(s_context.prefix + s_context.prefix_size, input, taken);
        s_context.prefix_size += taken;
        s_context.status.bytes_received += taken;
        input += taken;
        remaining -= taken;

        if (s_context.prefix_size == prefix_required) {
            result = ota_validator_validate_prefix(s_context.prefix,
                                                   s_context.prefix_size,
                                                   s_ops->get_running_description(),
                                                   &s_context.status.image);
            if (result == ESP_OK) {
                result = s_ops->get_next_update_partition(&s_context.status.target_partition);
            }
            if (result == ESP_OK &&
                s_context.status.expected_size != OTA_IMAGE_SIZE_UNKNOWN &&
                s_context.status.expected_size > s_context.status.target_partition.size) {
                result = ESP_ERR_INVALID_SIZE;
            }
            if (result == ESP_OK) {
                result = s_ops->session_begin(&s_context.session,
                                              &s_context.status.target_partition,
                                              s_context.status.expected_size);
            }
            if (result == ESP_OK) {
                result = s_ops->session_write(&s_context.session,
                                              s_context.prefix,
                                              s_context.prefix_size);
            }
            if (result != ESP_OK) {
                result = fail_locked(result, &events);
                unlock_service();
                event_batch_dispatch(&events);
                return result;
            }
            transition_locked(OTA_STATE_RECEIVING, &events);
        }
    }

    if (remaining != 0U) {
        result = s_ops->session_write(&s_context.session, input, remaining);
        if (result != ESP_OK) {
            result = fail_locked(result, &events);
            unlock_service();
            event_batch_dispatch(&events);
            return result;
        }
        s_context.status.bytes_received += remaining;
    }

    event_queue_locked(&events, OTA_EVENT_PROGRESS);
    unlock_service();
    event_batch_dispatch(&events);
    return ESP_OK;
}

esp_err_t ota_service_finish(void)
{
    ota_event_batch_t events;
    esp_err_t result;

    lock_service();
    event_batch_begin_locked(&events);

    if (s_context.status.state == OTA_STATE_ADMISSION) {
        result = fail_locked(ESP_ERR_INVALID_SIZE, &events);
        unlock_service();
        event_batch_dispatch(&events);
        return result;
    }
    if (s_context.status.state != OTA_STATE_RECEIVING) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }
    if (!ota_session_expected_complete(s_context.status.expected_size,
                                       s_context.status.bytes_received)) {
        result = fail_locked(ESP_ERR_INVALID_SIZE, &events);
        unlock_service();
        event_batch_dispatch(&events);
        return result;
    }

    transition_locked(OTA_STATE_VERIFYING, &events);
    result = s_ops->session_finish(&s_context.session);
    if (result != ESP_OK) {
        result = fail_locked(result, &events);
        unlock_service();
        event_batch_dispatch(&events);
        return result;
    }

    s_context.status.image.size = (uint32_t)s_context.status.bytes_received;
    s_context.status.has_staged_image = true;
    s_context.status.last_error = ESP_OK;
    transition_locked(OTA_STATE_STAGED, &events);
    event_queue_locked(&events, OTA_EVENT_STAGED);
    event_queue_locked(&events, OTA_EVENT_INSTALL_READY);
    unlock_service();
    event_batch_dispatch(&events);
    return ESP_OK;
}

esp_err_t ota_service_abort(void)
{
    ota_event_batch_t events;

    lock_service();
    event_batch_begin_locked(&events);
    if (s_context.status.state != OTA_STATE_ADMISSION &&
        s_context.status.state != OTA_STATE_RECEIVING &&
        s_context.status.state != OTA_STATE_VERIFYING) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ops->session_is_active(&s_context.session)) {
        (void)s_ops->session_abort(&s_context.session);
    }
    clear_transaction_locked();
    event_queue_locked(&events, OTA_EVENT_STATE_CHANGED);
    unlock_service();
    event_batch_dispatch(&events);
    return ESP_OK;
}

esp_err_t ota_service_discard(void)
{
    ota_event_batch_t events;

    lock_service();
    event_batch_begin_locked(&events);
    if (s_context.status.state != OTA_STATE_STAGED &&
        s_context.status.state != OTA_STATE_FAILED) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }

    clear_transaction_locked();
    event_queue_locked(&events, OTA_EVENT_STATE_CHANGED);
    unlock_service();
    event_batch_dispatch(&events);
    return ESP_OK;
}

esp_err_t ota_service_install(void)
{
    ota_event_batch_t events;
    esp_err_t result;

    lock_service();
    event_batch_begin_locked(&events);
    if (s_context.status.state != OTA_STATE_STAGED ||
        !s_context.status.has_staged_image) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }

    transition_locked(OTA_STATE_INSTALLING, &events);
    result = s_ops->set_boot_partition(&s_context.status.target_partition);
    if (result != ESP_OK) {
        result = fail_locked(result, &events);
        unlock_service();
        event_batch_dispatch(&events);
        return result;
    }

    transition_locked(OTA_STATE_REBOOT_PENDING, &events);
    event_queue_locked(&events, OTA_EVENT_REBOOT_PENDING);
    unlock_service();
    event_batch_dispatch(&events);
    return ESP_OK;
}

esp_err_t ota_service_get_status(ota_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_service();
    if (!s_context.initialized) {
        unlock_service();
        return ESP_ERR_INVALID_STATE;
    }
    snapshot_locked();
    *out_status = s_context.status;
    unlock_service();
    return ESP_OK;
}

esp_err_t ota_service_set_event_callback(ota_event_callback_t callback, void *context)
{
    lock_service();
    s_context.callback = callback;
    s_context.callback_context = context;
    unlock_service();
    return ESP_OK;
}

void ota_service_test_set_ops(const ota_service_ops_t *ops)
{
    s_ops = ops != NULL ? ops : &s_default_ops;
}

void ota_service_test_reset_ops(void)
{
    s_ops = &s_default_ops;
}

void ota_service_test_force_reset(void)
{
    lock_service();
    if (s_ops->session_is_active(&s_context.session)) {
        (void)s_ops->session_abort(&s_context.session);
    }
    clear_transaction_locked();
    s_context.initialized = true;
    unlock_service();
}