#ifndef CALLBOX_CONFIG_PORTAL_PRIVATE_H
#define CALLBOX_CONFIG_PORTAL_PRIVATE_H

#include <stdbool.h>
#include "esp_http_server.h"

bool config_portal_request_is_authorized(httpd_req_t *req);
void config_portal_set_ota_activity(bool active);

#endif /* CALLBOX_CONFIG_PORTAL_PRIVATE_H */
