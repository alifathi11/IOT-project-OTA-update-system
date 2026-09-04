#pragma once 

#include "esp_err.h"

esp_err_t ota_print_partition_info(void);

esp_err_t ota_install_from_url(
    const char *firmware_url,
    const char *expected_sha256);