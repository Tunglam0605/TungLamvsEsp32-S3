#ifndef CALLBOX_OTA_POLICY_PRIVATE_H
#define CALLBOX_OTA_POLICY_PRIVATE_H

#include "ota_policy.h"
#include "ota_types.h"
#include "status.h"

esp_err_t ota_policy_evaluate_snapshot(ota_policy_mode_t mode,
                                       ota_policy_source_t source,
                                       ota_policy_action_t action,
                                       bool authenticated,
                                       const callbox_status_t *callbox,
                                       const ota_status_t *ota,
                                       ota_policy_decision_t *out_decision);

#endif /* CALLBOX_OTA_POLICY_PRIVATE_H */
