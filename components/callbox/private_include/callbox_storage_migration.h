#ifndef CALLBOX_STORAGE_MIGRATION_H
#define CALLBOX_STORAGE_MIGRATION_H

#include "esp_err.h"

/** Initialise cfg/runtime NVS and copy the legacy schema exactly once.
 * The legacy partition is read-only migration input and is never erased. */
esp_err_t callbox_storage_migrate(void);

#endif
