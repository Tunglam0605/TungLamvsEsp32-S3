#include "ota_policy.h"
#include "ota_policy_private.h"

#include "ota_service.h"
#include "status.h"

static volatile ota_policy_mode_t s_mode = OTA_POLICY_MODE_NORMAL;

static void deny(ota_policy_decision_t *decision, ota_policy_reason_t reason)
{
    decision->allowed = false;
    decision->reason = reason;
}

static bool transaction_state_matches(ota_policy_action_t action, ota_state_t state)
{
    switch (action) {
    case OTA_POLICY_ACTION_BEGIN:
        return state == OTA_STATE_IDLE;
    case OTA_POLICY_ACTION_INSTALL:
        return state == OTA_STATE_STAGED;
    case OTA_POLICY_ACTION_DISCARD:
        return state == OTA_STATE_STAGED || state == OTA_STATE_FAILED;
    default:
        return false;
    }
}

esp_err_t ota_policy_evaluate_snapshot(ota_policy_mode_t mode,
                                       ota_policy_source_t source,
                                       ota_policy_action_t action,
                                       bool authenticated,
                                       const callbox_status_t *callbox,
                                       const ota_status_t *ota,
                                       ota_policy_decision_t *out_decision)
{
    if (!callbox || !ota || !out_decision) return ESP_ERR_INVALID_ARG;
    out_decision->allowed = false;
    out_decision->reason = OTA_POLICY_DENY_OTA_STATE;

    if (source == OTA_POLICY_SOURCE_WEB_LOCAL && !authenticated) {
        deny(out_decision, OTA_POLICY_DENY_UNAUTHORIZED);
        return ESP_OK;
    }
    if (!transaction_state_matches(action, ota->state)) {
        deny(out_decision, OTA_POLICY_DENY_OTA_STATE);
        return ESP_OK;
    }

    if (mode == OTA_POLICY_MODE_RECOVERY) {
        /* Recovery is deliberately local-only: no business runtime, WCS or
         * remote trigger is trusted to authorize a recovery flash. */
        if (source != OTA_POLICY_SOURCE_WEB_LOCAL || !authenticated) {
            deny(out_decision, OTA_POLICY_DENY_SOURCE);
            return ESP_OK;
        }
        out_decision->allowed = true;
        out_decision->reason = OTA_POLICY_ALLOW;
        return ESP_OK;
    }

    if (callbox->Mission[0] != TASK_IDLE || callbox->Mission[1] != TASK_IDLE ||
        callbox->Call[0].pending || callbox->Call[1].pending || callbox->Cancel.pending) {
        deny(out_decision, OTA_POLICY_DENY_MISSION_ACTIVE);
        return ESP_OK;
    }
    if (callbox->CommState != COMM_READY) {
        deny(out_decision, OTA_POLICY_DENY_COMMUNICATION);
        return ESP_OK;
    }

    out_decision->allowed = true;
    out_decision->reason = OTA_POLICY_ALLOW;
    return ESP_OK;
}

void ota_policy_set_mode(ota_policy_mode_t mode)
{
    s_mode = mode;
}

ota_policy_mode_t ota_policy_get_mode(void)
{
    return s_mode;
}

esp_err_t ota_policy_evaluate(ota_policy_source_t source,
                              ota_policy_action_t action,
                              bool authenticated,
                              ota_policy_decision_t *out_decision)
{
    if (!out_decision) return ESP_ERR_INVALID_ARG;
    callbox_status_t callbox;
    ota_status_t ota;
    status_get_snapshot(&callbox);
    esp_err_t err = ota_service_get_status(&ota);
    if (err != ESP_OK) return err;
    return ota_policy_evaluate_snapshot(s_mode, source, action, authenticated,
                                        &callbox, &ota, out_decision);
}

const char *ota_policy_reason_name(ota_policy_reason_t reason)
{
    switch (reason) {
    case OTA_POLICY_ALLOW: return "allowed";
    case OTA_POLICY_DENY_UNAUTHORIZED: return "unauthorized";
    case OTA_POLICY_DENY_SOURCE: return "source_not_allowed";
    case OTA_POLICY_DENY_MISSION_ACTIVE: return "mission_active";
    case OTA_POLICY_DENY_COMMUNICATION: return "communication_not_ready";
    case OTA_POLICY_DENY_OTA_STATE: return "ota_state_not_ready";
    default: return "unknown";
    }
}
