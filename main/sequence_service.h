/**
 * @file sequence_service.h
 * @brief Durable, device-wide event sequence allocator.
 *
 * A sequence identifies one logical event transaction. Every new event gets
 * a new value; retries keep the value stored by their transaction.
 */
#ifndef CALLBOX_SEQUENCE_SERVICE_H
#define CALLBOX_SEQUENCE_SERVICE_H

#include <stdint.h>
#include "esp_err.h"

/** Load the persisted high-watermark. Call once after nvs_storage_init(). */
esp_err_t sequence_service_init(void);

/** Allocate and persist the next device-wide event sequence number. */
esp_err_t sequence_next(uint32_t *sequence);

/** Return the last successfully persisted sequence number. */
uint32_t sequence_current(void);

#endif /* CALLBOX_SEQUENCE_SERVICE_H */
