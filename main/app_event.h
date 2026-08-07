/**
 * @file app_event.h
 * @brief Transport-neutral events delivered to the application owner task.
 */
#ifndef CALLBOX_APP_EVENT_H
#define CALLBOX_APP_EVENT_H

#include "protocol_types.h"

typedef enum {
    BTN_IDLE = 0,
    BTN_PRESSED,
    BTN_RELEASED,
} ButtonState_t;

typedef struct {
    int button_id;
    ButtonState_t state;
    uint32_t timestamp;
} ButtonMsg_t;

typedef enum {
    APP_EVENT_NONE = 0,
    APP_EVENT_WCS_COMMAND,
    APP_EVENT_MQTT_CONNECTED,
    APP_EVENT_MQTT_DISCONNECTED,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    protocol_command_t command;
} app_event_t;

#endif /* CALLBOX_APP_EVENT_H */
