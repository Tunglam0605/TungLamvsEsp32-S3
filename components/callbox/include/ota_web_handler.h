#ifndef CALLBOX_OTA_WEB_HANDLER_H
#define CALLBOX_OTA_WEB_HANDLER_H

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t ota_web_handler_register(httpd_handle_t server);

#endif /* CALLBOX_OTA_WEB_HANDLER_H */
