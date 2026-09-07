#ifndef OTA_VALIDATOR_H
#define OTA_VALIDATOR_H

#include <stdbool.h>

bool ota_validate_firmware(
    const char *version
);

#endif
