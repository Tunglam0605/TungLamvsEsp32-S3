/**
 * @file time_sync.h
 * @brief Shared SNTP clock service for every Callbox network mode.
 */
#ifndef CALLBOX_TIME_SYNC_H
#define CALLBOX_TIME_SYNC_H

#include <stdbool.h>
#include "queues.h"

/** Start background SNTP polling. Safe to call more than once. */
void time_sync_init(void);

/** True only after the RTC contains a plausible Unix timestamp. */
bool time_sync_is_valid(void);

/** Replace SNTP servers after validated portal configuration is saved. */
void time_sync_reconfigure(const Config_t *config);

#endif /* CALLBOX_TIME_SYNC_H */
