#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "gateway_config.h"

#define GATEWAY_IDENTITY_SEGMENT_MAX 31U
#define GATEWAY_TOPIC_MAX 192U

typedef struct {
    char status_json[GATEWAY_TOPIC_MAX];
    char status_bits[GATEWAY_TOPIC_MAX];
} gateway_topic_set_t;

bool gateway_topic_segment_valid(const char *segment);
bool gateway_warehouse_name_valid(const char *name);
bool gateway_identity_valid(const gateway_config_t *config);
esp_err_t gateway_topic_build_set(const gateway_config_t *config,
                                  gateway_topic_set_t *topics);
/* Migration helper only.  The legacy availability topic is not part of the
 * current external contract; it is built solely to delete a retained value
 * left by older firmware. */
esp_err_t gateway_topic_build_legacy_availability(const gateway_config_t *config,
                                                  char *topic,
                                                  size_t capacity);
