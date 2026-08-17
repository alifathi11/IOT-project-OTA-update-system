#pragma once 

#include "esp_err.h"

esp_err_t api_register_device(
    const char * device_id,
    const char * firmware_version
);

esp_err_t api_check_for_update(
    const char * device_id
);