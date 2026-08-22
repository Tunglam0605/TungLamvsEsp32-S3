#ifndef CALLBOX_OTA_BOOT_VALIDATOR_PRIVATE_H
#define CALLBOX_OTA_BOOT_VALIDATOR_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>
#include "health_monitor.h"

bool ota_boot_validator_normal_tasks_progressed(
    const uint32_t before[HEALTH_TASK_COUNT],
    const uint32_t after[HEALTH_TASK_COUNT]);

#endif
