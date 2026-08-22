#include <string.h>

#include "unity.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "ota_service_private.h"

#define TEST_PREFIX_SIZE (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static esp_app_desc_t s_running;
static bool s_description_available;
static esp_err_t s_target_result;
static esp_err_t s_begin_result;
static esp_err_t s_write_result;
static esp_err_t s_finish_result;
static esp_err_t s_abort_result;
static esp_err_t s_boot_result;
static int s_target_calls;
static int s_begin_calls;
static int s_write_calls;
static int s_finish_calls;
static int s_abort_calls;
static int s_boot_calls;
static bool s_active;
static int s_event_count;
static esp_err_t s_callback_status_result;
static ota_event_type_t s_last_event_type;
static ota_state_t s_last_event_state;

static esp_err_t fake_target(platform_ota_partition_t *target)
{
    s_target_calls++;
    if (s_target_result != ESP_OK) {
        return s_target_result;
    }
    memset(target, 0, sizeof(*target));
    target->size = 4096;
    target->address = 0x6A0000;
    target->slot = PLATFORM_OTA_SLOT_1;
    strcpy(target->label, "ota_1");
    return ESP_OK;
}

static esp_err_t fake_begin(platform_ota_session_t *session,
                            const platform_ota_partition_t *target,
                            size_t size)
{
    (void)target;
    (void)size;
    s_begin_calls++;
    if (s_begin_result != ESP_OK) {
        return s_begin_result;
    }
    platform_ota_session_init(session);
    session->_active = 1U;
    s_active = true;
    return ESP_OK;
}

static esp_err_t fake_write(platform_ota_session_t *session, const void *data, size_t size)
{
    (void)session;
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_GREATER_THAN(0U, size);
    s_write_calls++;
    return s_write_result;
}

static esp_err_t fake_finish(platform_ota_session_t *session)
{
    s_finish_calls++;
    s_active = false;
    session->_active = 0U;
    return s_finish_result;
}

static esp_err_t fake_abort(platform_ota_session_t *session)
{
    s_abort_calls++;
    s_active = false;
    session->_active = 0U;
    return s_abort_result;
}

static bool fake_active(const platform_ota_session_t *session)
{
    (void)session;
    return s_active;
}

static esp_err_t fake_boot(const platform_ota_partition_t *target)
{
    TEST_ASSERT_NOT_NULL(target);
    s_boot_calls++;
    return s_boot_result;
}

static const esp_app_desc_t *fake_desc(void)
{
    return s_description_available ? &s_running : NULL;
}

static const ota_service_ops_t s_ops = {
    .get_next_update_partition = fake_target,
    .session_begin = fake_begin,
    .session_write = fake_write,
    .session_finish = fake_finish,
    .session_abort = fake_abort,
    .session_is_active = fake_active,
    .set_boot_partition = fake_boot,
    .get_running_description = fake_desc,
};

static void reentrant_event(const ota_event_t *event, void *context)
{
    (void)context;
    ota_status_t status;
    s_callback_status_result = ota_service_get_status(&status);
    s_event_count++;
    s_last_event_type = event->type;
    s_last_event_state = event->status.state;
}

static void reset_fake(void)
{
    ota_service_test_set_ops(&s_ops);
    s_active = false;
    ota_service_test_force_reset();
    (void)ota_service_set_event_callback(NULL, NULL);

    memset(&s_running, 0, sizeof(s_running));
    s_running.magic_word = ESP_APP_DESC_MAGIC_WORD;
    strcpy(s_running.project_name, "generic-app");
    strcpy(s_running.version, "running");
    s_description_available = true;

    s_target_result = ESP_OK;
    s_begin_result = ESP_OK;
    s_write_result = ESP_OK;
    s_finish_result = ESP_OK;
    s_abort_result = ESP_OK;
    s_boot_result = ESP_OK;
    s_target_calls = 0;
    s_begin_calls = 0;
    s_write_calls = 0;
    s_finish_calls = 0;
    s_abort_calls = 0;
    s_boot_calls = 0;
    s_event_count = 0;
    s_callback_status_result = ESP_FAIL;
    s_last_event_type = OTA_EVENT_STATE_CHANGED;
    s_last_event_state = OTA_STATE_IDLE;

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_init());
}

static void make_image(uint8_t *data, size_t size)
{
    TEST_ASSERT_GREATER_OR_EQUAL(TEST_PREFIX_SIZE, size);
    memset(data, 0, size);

    esp_image_header_t *header = (esp_image_header_t *)data;
    header->magic = ESP_IMAGE_HEADER_MAGIC;
    header->chip_id = CONFIG_IDF_FIRMWARE_CHIP_ID;

    esp_app_desc_t *desc = (esp_app_desc_t *)(data + sizeof(*header) +
                                               sizeof(esp_image_segment_header_t));
    desc->magic_word = ESP_APP_DESC_MAGIC_WORD;
    desc->secure_version = 7U;
    strcpy(desc->project_name, "generic-app");
    strcpy(desc->version, "1.2.3");
}

static esp_app_desc_t *image_desc(uint8_t *image)
{
    return (esp_app_desc_t *)(image + sizeof(esp_image_header_t) +
                              sizeof(esp_image_segment_header_t));
}

TEST_CASE("ota tiny chunks stage and install without reboot", "[ota]")
{
    reset_fake();
    uint8_t image[TEST_PREFIX_SIZE + 3U];
    make_image(image, sizeof(image));

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_service_begin(sizeof(image)));
    for (size_t i = 0; i < sizeof(image); ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(&image[i], 1U));
    }

    TEST_ASSERT_EQUAL(1, s_target_calls);
    TEST_ASSERT_EQUAL(1, s_begin_calls);
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_finish());

    ota_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_STAGED, status.state);
    TEST_ASSERT_TRUE(status.has_staged_image);
    TEST_ASSERT_EQUAL_UINT32(sizeof(image), status.image.size);
    TEST_ASSERT_EQUAL_STRING("generic-app", status.image.project_name);
    TEST_ASSERT_EQUAL_STRING("1.2.3", status.image.version);
    TEST_ASSERT_EQUAL_UINT32(7U, status.image.secure_version);
    TEST_ASSERT_EQUAL(0, s_boot_calls);

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_install());
    TEST_ASSERT_EQUAL(1, s_boot_calls);
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_REBOOT_PENDING, status.state);
    ota_service_test_force_reset();
}

TEST_CASE("ota admission rejects invalid metadata before flash", "[ota]")
{
    uint8_t image[TEST_PREFIX_SIZE];

    reset_fake();
    make_image(image, sizeof(image));
    image[0] = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_VALIDATE_FAILED, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(0, s_target_calls);
    TEST_ASSERT_EQUAL(0, s_begin_calls);

    reset_fake();
    make_image(image, sizeof(image));
    ((esp_image_header_t *)image)->chip_id = ESP_CHIP_ID_ESP32;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_VALIDATE_FAILED, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(0, s_begin_calls);

    reset_fake();
    make_image(image, sizeof(image));
    strcpy(image_desc(image)->project_name, "other-app");
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_VALIDATE_FAILED, ota_service_write(image, sizeof(image)));

    reset_fake();
    make_image(image, sizeof(image));
    image_desc(image)->version[0] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_VALIDATE_FAILED, ota_service_write(image, sizeof(image)));

    reset_fake();
    make_image(image, sizeof(image));
    s_description_available = false;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(0, s_begin_calls);
}

TEST_CASE("ota lifecycle guards active transaction", "[ota]")
{
    reset_fake();
    uint8_t image[TEST_PREFIX_SIZE];
    make_image(image, sizeof(image));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ota_service_begin(0U));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(NULL, 0U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_service_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_service_install());
    TEST_ASSERT_EQUAL(0, s_boot_calls);

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_TRUE(s_active);
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_reset());
    TEST_ASSERT_FALSE(s_active);
    TEST_ASSERT_EQUAL(1, s_abort_calls);

    ota_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_IDLE, status.state);
}

TEST_CASE("ota supports unknown size and rejects known truncation", "[ota]")
{
    reset_fake();
    uint8_t image[TEST_PREFIX_SIZE + 5U];
    make_image(image, sizeof(image));

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image) + 1U));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ota_service_finish());
    TEST_ASSERT_EQUAL(1, s_abort_calls);
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_discard());

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(OTA_IMAGE_SIZE_UNKNOWN));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_finish());

    ota_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_STAGED, status.state);
    TEST_ASSERT_EQUAL_UINT32(sizeof(image), status.image.size);
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_discard());
}

TEST_CASE("ota cleans write and finalization failures", "[ota]")
{
    reset_fake();
    uint8_t image[TEST_PREFIX_SIZE];
    make_image(image, sizeof(image));
    s_write_result = ESP_FAIL;

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_FAIL, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(1, s_abort_calls);

    ota_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_FAILED, status.state);
    TEST_ASSERT_EQUAL(ESP_FAIL, status.last_error);

    reset_fake();
    make_image(image, sizeof(image));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(image, sizeof(image)));
    s_finish_result = ESP_ERR_OTA_VALIDATE_FAILED;
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_VALIDATE_FAILED, ota_service_finish());
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_FAILED, status.state);
    TEST_ASSERT_FALSE(status.has_staged_image);
    TEST_ASSERT_EQUAL(0, s_boot_calls);
}

TEST_CASE("ota provider failures never stage or activate", "[ota]")
{
    uint8_t image[TEST_PREFIX_SIZE];

    reset_fake();
    make_image(image, sizeof(image));
    s_target_result = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(0, s_begin_calls);
    TEST_ASSERT_EQUAL(0, s_boot_calls);

    reset_fake();
    make_image(image, sizeof(image));
    s_begin_result = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(1, s_begin_calls);
    TEST_ASSERT_EQUAL(0, s_write_calls);
    TEST_ASSERT_EQUAL(0, s_boot_calls);
}

TEST_CASE("ota callbacks are reentrant safe and abort returns idle", "[ota]")
{
    reset_fake();
    uint8_t image[TEST_PREFIX_SIZE];
    make_image(image, sizeof(image));

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_set_event_callback(reentrant_event, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(OTA_IMAGE_SIZE_UNKNOWN));
    TEST_ASSERT_GREATER_THAN(0, s_event_count);
    TEST_ASSERT_EQUAL(ESP_OK, s_callback_status_result);

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, s_callback_status_result);
    TEST_ASSERT_TRUE(s_active);
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_abort());
    TEST_ASSERT_EQUAL(1, s_abort_calls);

    ota_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_IDLE, status.state);
    TEST_ASSERT_EQUAL(OTA_STATE_IDLE, s_last_event_state);
}

TEST_CASE("ota reboot pending cannot be reset and boot failure is discardable", "[ota]")
{
    reset_fake();
    uint8_t image[TEST_PREFIX_SIZE];
    make_image(image, sizeof(image));

    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_finish());
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_install());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_service_reset());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_service_init());

    ota_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_REBOOT_PENDING, status.state);
    ota_service_test_force_reset();

    reset_fake();
    make_image(image, sizeof(image));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_begin(sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_write(image, sizeof(image)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_finish());
    s_boot_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, ota_service_install());
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_FAILED, status.state);
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_discard());
    TEST_ASSERT_EQUAL(ESP_OK, ota_service_get_status(&status));
    TEST_ASSERT_EQUAL(OTA_STATE_IDLE, status.state);
}
