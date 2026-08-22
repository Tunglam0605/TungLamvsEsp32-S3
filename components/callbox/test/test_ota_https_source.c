#include "unity.h"

#include "ota_https_source_private.h"

TEST_CASE("HTTPS OTA manifest accepts required fields and optional size", "[callbox][ota][https]")
{
    static const char json[] =
        "{\"firmware_url\":\"https://updates.example.test/callbox.bin\","
        "\"project\":\"callbox_sews\",\"version\":\"1.2.3\",\"size\":123456}";
    ota_https_manifest_t manifest;
    TEST_ASSERT_EQUAL(ESP_OK, ota_https_manifest_parse(json, sizeof(json) - 1U, &manifest));
    TEST_ASSERT_EQUAL_STRING("https://updates.example.test/callbox.bin", manifest.firmware_url);
    TEST_ASSERT_EQUAL_STRING("callbox_sews", manifest.project);
    TEST_ASSERT_EQUAL_STRING("1.2.3", manifest.version);
    TEST_ASSERT_TRUE(manifest.has_size);
    TEST_ASSERT_EQUAL_UINT32(123456U, manifest.size);
}

TEST_CASE("HTTPS OTA manifest rejects malformed JSON and missing fields", "[callbox][ota][https]")
{
    ota_https_manifest_t manifest;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ota_https_manifest_parse("{\"project\":", 11U, &manifest));
    static const char missing[] = "{\"firmware_url\":\"https://u.example/a.bin\",\"project\":\"callbox\"}";
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ota_https_manifest_parse(missing, sizeof(missing) - 1U, &manifest));
}

TEST_CASE("HTTPS OTA manifest preserves project for caller mismatch rejection", "[callbox][ota][https]")
{
    static const char json[] =
        "{\"firmware_url\":\"https://u.example/a.bin\",\"project\":\"other_product\",\"version\":\"1\"}";
    ota_https_manifest_t manifest;
    TEST_ASSERT_EQUAL(ESP_OK, ota_https_manifest_parse(json, sizeof(json) - 1U, &manifest));
    TEST_ASSERT_FALSE(ota_https_manifest_matches_project(&manifest, "callbox_sews"));
    TEST_ASSERT_TRUE(ota_https_manifest_matches_project(&manifest, "other_product"));
}

TEST_CASE("HTTPS OTA manifest rejects HTTP URL", "[callbox][ota][https]")
{
    static const char json[] =
        "{\"firmware_url\":\"http://u.example/a.bin\",\"project\":\"callbox_sews\",\"version\":\"1\"}";
    ota_https_manifest_t manifest;
    TEST_ASSERT_FALSE(ota_https_url_is_secure("http://u.example/a.bin"));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ota_https_manifest_parse(json, sizeof(json) - 1U, &manifest));
}

TEST_CASE("HTTPS OTA manifest validates size syntax", "[callbox][ota][https]")
{
    ota_https_manifest_t manifest;
    static const char valid[] =
        "{\"firmware_url\":\"https://u.example/a.bin\",\"project\":\"callbox_sews\",\"version\":\"1\",\"size\":1}";
    static const char invalid[] =
        "{\"firmware_url\":\"https://u.example/a.bin\",\"project\":\"callbox_sews\",\"version\":\"1\",\"size\":0}";
    TEST_ASSERT_EQUAL(ESP_OK, ota_https_manifest_parse(valid, sizeof(valid) - 1U, &manifest));
    TEST_ASSERT_TRUE(manifest.has_size);
    TEST_ASSERT_EQUAL_UINT32(1U, manifest.size);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ota_https_manifest_parse(invalid, sizeof(invalid) - 1U, &manifest));
}
