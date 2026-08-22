#ifndef OTA_EVENTS_H
#define OTA_EVENTS_H

#include "ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_EVENT_STATE_CHANGED = 0,
    OTA_EVENT_PROGRESS,
    OTA_EVENT_STAGED,
    OTA_EVENT_FAILED,
    OTA_EVENT_INSTALL_READY,
    OTA_EVENT_REBOOT_PENDING,
} ota_event_type_t;

typedef struct {
    ota_event_type_t type;
    ota_status_t status;
} ota_event_t;

typedef void (*ota_event_callback_t)(const ota_event_t *event, void *context);

#ifdef __cplusplus
}
#endif

#endif /* OTA_EVENTS_H */
