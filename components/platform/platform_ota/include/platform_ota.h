/**
 * @file platform_ota.h
 * @brief Industrial Dual-Mode OTA Platform Layer for ESP32-S3.
 *
 * Supports:
 * - Local Stream/Web Upload OTA (Web Portal / REST API)
 * - Remote URL/MQTT OTA (Fleet-scale mass management)
 * - Anti-Brick Self-Testing & Rollback protection
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_OTA_STATE_IDLE = 0,
    PLATFORM_OTA_STATE_STREAMING,
    PLATFORM_OTA_STATE_DOWNLOADING,
    PLATFORM_OTA_STATE_VERIFYING,
    PLATFORM_OTA_STATE_SUCCESS_REBOOTING,
    PLATFORM_OTA_STATE_ERROR
} platform_ota_state_t;

typedef struct {
    char running_partition[16];
    char next_partition[16];
    char app_version[32];
    char project_name[32];
    char compile_time[32];
    platform_ota_state_t state;
    int progress_percent;
    size_t bytes_written;
    size_t total_bytes;
    char error_msg[64];
} platform_ota_info_t;

typedef void (*platform_ota_progress_cb_t)(int progress_percent, size_t bytes_written, size_t total_bytes, void *user_ctx);

/**
 * @brief Initialize OTA subsystem, inspect partition state, and handle rollback state.
 */
esp_err_t platform_ota_init(void);

/**
 * @brief Confirm that current firmware is healthy and cancel rollback.
 */
esp_err_t platform_ota_mark_valid(void);

/**
 * @brief Get current OTA info and status.
 */
esp_err_t platform_ota_get_info(platform_ota_info_t *out_info);

/**
 * @brief Start local streaming OTA write (e.g. from Web upload).
 * @param image_size Expected total image size in bytes (or OTA_SIZE_UNKNOWN).
 */
esp_err_t platform_ota_stream_begin(size_t image_size);

/**
 * @brief Write incoming chunk of firmware binary.
 */
esp_err_t platform_ota_stream_write(const void *data, size_t length);

/**
 * @brief Finish local streaming OTA write, validate image, set boot partition, and trigger reboot.
 */
esp_err_t platform_ota_stream_finish(void);

/**
 * @brief Abort currently running local streaming OTA write.
 */
void platform_ota_stream_abort(void);

/**
 * @brief Start background HTTPS/HTTP download OTA task from URL.
 */
esp_err_t platform_ota_start_from_url(const char *url, platform_ota_progress_cb_t progress_cb, void *user_ctx);

#ifdef __cplusplus
}
#endif
