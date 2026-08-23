#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define OTA_VERSION_SIZE 32
#define OTA_URL_SIZE     320
#define OTA_SHA256_SIZE  65

typedef struct
{
    bool update_available;

    int update_id;
    int firmware_id;

    char target_version[OTA_VERSION_SIZE];
    char file_url[OTA_URL_SIZE];
    char sha256[OTA_SHA256_SIZE];

    size_t file_size;

} ota_update_info_t;


esp_err_t api_register_device(
    const char *device_id,
    const char *firmware_version
);


esp_err_t api_check_for_update(
    const char *device_id,
    const char *firmware_version,
    ota_update_info_t *update_info
);

esp_err_t api_report_update_status(
    int update_id,
    const char *status,
    const char *message
);