#ifndef CALLBOX_OTA_POLICY_H
#define CALLBOX_OTA_POLICY_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_POLICY_MODE_NORMAL = 0,
    OTA_POLICY_MODE_RECOVERY,
} ota_policy_mode_t;

typedef enum {
    OTA_POLICY_SOURCE_WEB_LOCAL = 0,
    OTA_POLICY_SOURCE_INTERNAL_TRIGGER,
    OTA_POLICY_SOURCE_SERVICE_TOOL,
} ota_policy_source_t;

typedef enum {
    OTA_POLICY_ACTION_BEGIN = 0,
    OTA_POLICY_ACTION_INSTALL,
    OTA_POLICY_ACTION_DISCARD,
} ota_policy_action_t;

typedef enum {
    OTA_POLICY_ALLOW = 0,
    OTA_POLICY_DENY_UNAUTHORIZED,
    OTA_POLICY_DENY_SOURCE,
    OTA_POLICY_DENY_MISSION_ACTIVE,
    OTA_POLICY_DENY_COMMUNICATION,
    OTA_POLICY_DENY_OTA_STATE,
} ota_policy_reason_t;

typedef struct {
    bool allowed;
    ota_policy_reason_t reason;
} ota_policy_decision_t;

void ota_policy_set_mode(ota_policy_mode_t mode);
ota_policy_mode_t ota_policy_get_mode(void);
esp_err_t ota_policy_evaluate(ota_policy_source_t source,
                              ota_policy_action_t action,
                              bool authenticated,
                              ota_policy_decision_t *out_decision);
const char *ota_policy_reason_name(ota_policy_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* CALLBOX_OTA_POLICY_H */
