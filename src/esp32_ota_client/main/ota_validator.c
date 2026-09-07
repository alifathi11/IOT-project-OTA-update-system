#include "ota_validator.h"

bool ota_validate_firmware(
    const char *version)
{
    /*
     * Stage 3.1 uses the production/success path.
     * Replace this later with real application health checks.
     */
    (void)version;

    return true;
}
