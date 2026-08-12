#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the read-only Ethernet/Wi-Fi diagnostic dashboard. Idempotent. */
esp_err_t debug_http_server_start(void);

#ifdef __cplusplus
}
#endif
