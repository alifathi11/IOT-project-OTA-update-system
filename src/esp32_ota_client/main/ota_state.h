#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define OTA_STATE_VERSION_SIZE 32

typedef struct
{
    bool pending;
    int update_id;
    char target_version[OTA_STATE_VERSION_SIZE];
} ota_state_t;

esp_err_t ota_state_save(
    int update_id,
    const char *target_version
);

esp_err_t ota_state_load(
    ota_state_t *state
);

esp_err_t ota_state_clear(void);