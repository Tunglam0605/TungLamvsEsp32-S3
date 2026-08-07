/** @file app_event_queue.h @brief Queue plumbing for application events. */
#ifndef CALLBOX_APP_EVENT_QUEUE_H
#define CALLBOX_APP_EVENT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include "app_event.h"
#include "esp_err.h"

esp_err_t app_event_queue_init(void);
bool app_event_send(const app_event_t *event, uint32_t timeout_ms);
bool app_event_receive(app_event_t *event, uint32_t timeout_ms);

#endif /* CALLBOX_APP_EVENT_QUEUE_H */
