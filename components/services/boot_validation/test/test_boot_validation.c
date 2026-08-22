#include "unity.h"
#include "boot_validation.h"
#include "boot_validation_private.h"

static platform_ota_image_state_t s_state;
static int s_mark_valid_calls;
static int s_rollback_calls;
static esp_err_t fake_running(platform_ota_partition_t *out) { out->_native = (const void *)1; return ESP_OK; }
static esp_err_t fake_state(const platform_ota_partition_t *partition, platform_ota_image_state_t *out)
{ (void)partition; *out = s_state; return ESP_OK; }
static esp_err_t fake_valid(void) { ++s_mark_valid_calls; return ESP_OK; }
static esp_err_t fake_rollback(void) { ++s_rollback_calls; return ESP_OK; }
static const boot_validation_ops_t s_fake_ops = {
    .get_running_partition = fake_running, .get_partition_state = fake_state,
    .mark_running_valid = fake_valid, .mark_running_invalid_and_rollback_reboot = fake_rollback,
};

void setUp(void) { s_state = PLATFORM_OTA_IMG_PENDING_VERIFY; s_mark_valid_calls = s_rollback_calls = 0; boot_validation_test_set_ops(&s_fake_ops); }
void tearDown(void) { boot_validation_test_reset(); }

TEST_CASE("pending verify is detected and qualified image is marked valid", "[boot_validation]")
{
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_init());
    TEST_ASSERT_TRUE(boot_validation_is_pending());
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_mark_valid());
    TEST_ASSERT_EQUAL(1, s_mark_valid_calls);
    TEST_ASSERT_FALSE(boot_validation_is_pending());
    TEST_ASSERT_EQUAL(BOOT_VALIDATION_VALID, boot_validation_get_lifecycle());
}
TEST_CASE("controlled local failure requests rollback only while pending", "[boot_validation]")
{
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_init());
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_request_rollback());
    TEST_ASSERT_EQUAL(1, s_rollback_calls);
}
TEST_CASE("unconfirmed pending boot leaves rollback decision to bootloader after reset", "[boot_validation]")
{
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_init());
    TEST_ASSERT_TRUE(boot_validation_is_pending());
    /* This models a panic/WDT/power-loss before app qualification: no mark
     * call is made, so ESP-IDF bootloader still sees PENDING_VERIFY. */
    TEST_ASSERT_EQUAL(0, s_mark_valid_calls);
    TEST_ASSERT_EQUAL(0, s_rollback_calls);
    TEST_ASSERT_EQUAL(BOOT_VALIDATION_PENDING, boot_validation_get_lifecycle());
}
TEST_CASE("normal valid boot is a rollback validation no-op", "[boot_validation]")
{
    s_state = PLATFORM_OTA_IMG_VALID;
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_init());
    TEST_ASSERT_FALSE(boot_validation_is_pending());
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_mark_valid());
    TEST_ASSERT_EQUAL(ESP_OK, boot_validation_request_rollback());
    TEST_ASSERT_EQUAL(0, s_mark_valid_calls);
    TEST_ASSERT_EQUAL(0, s_rollback_calls);
}
