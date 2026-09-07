#include "ota_validator.h"

bool ota_validate_firmware(
    const char *version)
{
    /*
     * Production self-test hook.
     *
     * Add hardware/application health checks here later.
     * Returning false triggers ESP-IDF rollback.
     */
    (void)version;

    return true;
}
