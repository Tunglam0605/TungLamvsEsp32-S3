#include <string.h>

#include "unity.h"
#include "platform_ota.h"
#include "platform_ota_internal.h"

static esp_partition_t s_factory;
static esp_partition_t s_ota0;
static esp_partition_t s_ota1;
static const esp_partition_t *s_running;
static const esp_partition_t *s_next;
static esp_err_t s_begin_result;
static esp_err_t s_write_result;
static esp_err_t s_end_result;
static esp_err_t s_abort_result;
static esp_err_t s_boot_result;
static esp_err_t s_state_result;
static esp_err_t s_valid_result;
static esp_err_t s_rollback_result;
static esp_ota_img_states_t s_state;
static esp_ota_handle_t s_handle;
static size_t s_begin_size;
static int s_begin_calls;
static int s_write_calls;
static int s_end_calls;
static int s_abort_calls;
static int s_boot_calls;
static int s_valid_calls;
static int s_rollback_calls;

static const esp_partition_t *fake_running(void) { return s_running; }
static const esp_partition_t *fake_next(const esp_partition_t *start_from)
{
    (void)start_from;
    return s_next;
}
static esp_err_t fake_begin(const esp_partition_t *partition, size_t image_size,
                            esp_ota_handle_t *out_handle)
{
    TEST_ASSERT_EQUAL_PTR(s_next, partition);
    s_begin_calls++;
    s_begin_size = image_size;
    if (s_begin_result == ESP_OK) *out_handle = s_handle;
    return s_begin_result;
}
static esp_err_t fake_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    TEST_ASSERT_EQUAL_UINT32(s_handle, handle);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_GREATER_THAN(0, size);
    s_write_calls++;
    return s_write_result;
}
static esp_err_t fake_end(esp_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL_UINT32(s_handle, handle);
    s_end_calls++;
    return s_end_result;
}
static esp_err_t fake_abort(esp_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL_UINT32(s_handle, handle);
    s_abort_calls++;
    return s_abort_result;
}
static esp_err_t fake_boot(const esp_partition_t *partition)
{
    TEST_ASSERT_EQUAL_PTR(s_next, partition);
    s_boot_calls++;
    return s_boot_result;
}
static esp_err_t fake_state(const esp_partition_t *partition, esp_ota_img_states_t *state)
{
    TEST_ASSERT_NOT_NULL(partition);
    if (s_state_result == ESP_OK) *state = s_state;
    return s_state_result;
}
static esp_err_t fake_valid(void)
{
    s_valid_calls++;
    return s_valid_result;
}
static esp_err_t fake_rollback(void)
{
    s_rollback_calls++;
    return s_rollback_result;
}

static const platform_ota_ops_t s_fake_ops = {
    .get_running_partition = fake_running,
    .get_next_update_partition = fake_next,
    .begin = fake_begin,
    .write = fake_write,
    .end = fake_end,
    .abort = fake_abort,
    .set_boot_partition = fake_boot,
    .get_state_partition = fake_state,
    .mark_app_valid_cancel_rollback = fake_valid,
    .mark_app_invalid_rollback_and_reboot = fake_rollback,
};

static void reset_fakes(void)
{
    memset(&s_factory, 0, sizeof(s_factory));
    memset(&s_ota0, 0, sizeof(s_ota0));
    memset(&s_ota1, 0, sizeof(s_ota1));
    s_factory.type = ESP_PARTITION_TYPE_APP;
    s_factory.subtype = ESP_PARTITION_SUBTYPE_APP_FACTORY;
    s_factory.address = 0x10000;
    s_factory.size = 0x200000;
    strcpy(s_factory.label, "factory");
    s_ota0.type = ESP_PARTITION_TYPE_APP;
    s_ota0.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    s_ota0.address = 0x2A0000;
    s_ota0.size = 0x400000;
    strcpy(s_ota0.label, "ota_0");
    s_ota1.type = ESP_PARTITION_TYPE_APP;
    s_ota1.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
    s_ota1.address = 0x6A0000;
    s_ota1.size = 0x400000;
    strcpy(s_ota1.label, "ota_1");
    s_running = &s_factory;
    s_next = &s_ota0;
    s_begin_result = ESP_OK;
    s_write_result = ESP_OK;
    s_end_result = ESP_OK;
    s_abort_result = ESP_OK;
    s_boot_result = ESP_OK;
    s_state_result = ESP_OK;
    s_valid_result = ESP_OK;
    s_rollback_result = ESP_OK;
    s_state = ESP_OTA_IMG_VALID;
    s_handle = 77;
    s_begin_size = 0;
    s_begin_calls = s_write_calls = s_end_calls = s_abort_calls = 0;
    s_boot_calls = s_valid_calls = s_rollback_calls = 0;
    platform_ota_test_set_ops(&s_fake_ops);
}

TEST_CASE("platform_ota selects inactive OTA partition", "[platform_ota]")
{
    reset_fakes();
    platform_ota_partition_t part;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&part));
    TEST_ASSERT_EQUAL_STRING("ota_0", part.label);
    TEST_ASSERT_EQUAL(PLATFORM_OTA_SLOT_0, part.slot);
    TEST_ASSERT_EQUAL_HEX32(0x2A0000, part.address);
}

TEST_CASE("platform_ota rejects running partition as update target", "[platform_ota]")
{
    reset_fakes();
    s_running = &s_ota0;
    platform_ota_partition_t target = {
        .address = s_ota0.address, .size = s_ota0.size,
        .slot = PLATFORM_OTA_SLOT_0, ._native = &s_ota0,
    };
    platform_ota_session_t session;
    platform_ota_session_init(&session);
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_PARTITION_CONFLICT,
                      platform_ota_session_begin(&session, &target, 1024));
    TEST_ASSERT_FALSE(platform_ota_session_is_active(&session));
}

TEST_CASE("platform_ota streams known size and finishes", "[platform_ota]")
{
    reset_fakes();
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    platform_ota_session_t session;
    platform_ota_session_init(&session);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_begin(&session, &target, 8));
    TEST_ASSERT_EQUAL_UINT32(8, s_begin_size);
    uint8_t data[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_write(&session, data, sizeof(data)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, platform_ota_session_finish(&session));
    TEST_ASSERT_TRUE(platform_ota_session_is_active(&session));
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_write(&session, data, sizeof(data)));
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_finish(&session));
    TEST_ASSERT_FALSE(platform_ota_session_is_active(&session));
    TEST_ASSERT_EQUAL(2, s_write_calls);
    TEST_ASSERT_EQUAL(1, s_end_calls);
}

TEST_CASE("platform_ota zero write is provider no-op", "[platform_ota]")
{
    reset_fakes();
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    platform_ota_session_t session;
    platform_ota_session_init(&session);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_begin(&session, &target, 4));
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_write(&session, NULL, 0));
    TEST_ASSERT_EQUAL(0, s_write_calls);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_abort(&session));
}

TEST_CASE("platform_ota prevents writes past expected size", "[platform_ota]")
{
    reset_fakes();
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    platform_ota_session_t session;
    platform_ota_session_init(&session);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_begin(&session, &target, 4));
    uint8_t data[5] = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      platform_ota_session_write(&session, data, sizeof(data)));
    TEST_ASSERT_EQUAL(0, s_write_calls);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_abort(&session));
}

TEST_CASE("platform_ota closes session after end failure", "[platform_ota]")
{
    reset_fakes();
    s_end_result = ESP_ERR_OTA_VALIDATE_FAILED;
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    platform_ota_session_t session;
    platform_ota_session_init(&session);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_begin(&session, &target, 4));
    uint8_t data[4] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_write(&session, data, sizeof(data)));
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_VALIDATE_FAILED, platform_ota_session_finish(&session));
    TEST_ASSERT_FALSE(platform_ota_session_is_active(&session));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, platform_ota_session_abort(&session));
}

TEST_CASE("platform_ota closes session after abort failure", "[platform_ota]")
{
    reset_fakes();
    s_abort_result = ESP_ERR_NOT_FOUND;
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    platform_ota_session_t session;
    platform_ota_session_init(&session);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_begin(&session, &target, 4));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, platform_ota_session_abort(&session));
    TEST_ASSERT_FALSE(platform_ota_session_is_active(&session));
}

TEST_CASE("platform_ota unknown size uses sequential-write sentinel", "[platform_ota]")
{
    reset_fakes();
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    platform_ota_session_t session;
    platform_ota_session_init(&session);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_begin(
                                  &session, &target, PLATFORM_OTA_IMAGE_SIZE_UNKNOWN));
    TEST_ASSERT_EQUAL_UINT32(OTA_WITH_SEQUENTIAL_WRITES, s_begin_size);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_session_abort(&session));
}

TEST_CASE("platform_ota activation and rollback primitives delegate", "[platform_ota]")
{
    reset_fakes();
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_set_boot_partition(&target));
    TEST_ASSERT_EQUAL(1, s_boot_calls);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_mark_running_valid());
    TEST_ASSERT_EQUAL(1, s_valid_calls);
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_mark_running_invalid_and_rollback_reboot());
    TEST_ASSERT_EQUAL(1, s_rollback_calls);
}

TEST_CASE("platform_ota maps image state", "[platform_ota]")
{
    reset_fakes();
    platform_ota_partition_t target;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_next_update_partition(&target));
    s_state = ESP_OTA_IMG_PENDING_VERIFY;
    platform_ota_image_state_t state = PLATFORM_OTA_IMG_UNDEFINED;
    TEST_ASSERT_EQUAL(ESP_OK, platform_ota_get_partition_state(&target, &state));
    TEST_ASSERT_EQUAL(PLATFORM_OTA_IMG_PENDING_VERIFY, state);
}