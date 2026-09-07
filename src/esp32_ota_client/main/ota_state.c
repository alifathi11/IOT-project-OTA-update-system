#include "ota_state.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#define OTA_NAMESPACE "ota_state"

static const char *TAG = "ota_state";


static bool ota_status_is_valid(int32_t status)
{
    return status >= OTA_STATUS_IDLE &&
           status <= OTA_STATUS_ROLLED_BACK;
}


const char *ota_status_to_string(
    ota_status_t status)
{
    switch (status)
    {
        case OTA_STATUS_PENDING:
            return "pending";

        case OTA_STATUS_DOWNLOADING:
            return "downloading";

        case OTA_STATUS_VERIFIED:
            return "verified";

        case OTA_STATUS_INSTALLING:
            return "installing";

        case OTA_STATUS_VALIDATING:
            return "validating";

        case OTA_STATUS_SUCCESS:
            return "success";

        case OTA_STATUS_FAILED:
            return "failed";

        case OTA_STATUS_ROLLED_BACK:
            return "rolled_back";

        case OTA_STATUS_IDLE:
        default:
            return "idle";
    }
}


esp_err_t ota_state_save(
    int update_id,
    const char *target_version)
{
    if (update_id <= 0 || target_version == NULL)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        OTA_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (err != ESP_OK)
        return err;

    err = nvs_set_i32(
        handle,
        "update_id",
        update_id
    );

    if (err == ESP_OK)
    {
        err = nvs_set_str(
            handle,
            "target_ver",
            target_version
        );
    }

    if (err == ESP_OK)
    {
        err = nvs_set_i32(
            handle,
            "status",
            (int32_t)OTA_STATUS_PENDING
        );
    }

    if (err == ESP_OK)
        err = nvs_commit(handle);

    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "OTA state saved: update_id=%d target=%s status=%s",
            update_id,
            target_version,
            ota_status_to_string(OTA_STATUS_PENDING)
        );
    }

    return err;
}


esp_err_t ota_state_load(
    ota_state_t *state)
{
    if (state == NULL)
        return ESP_ERR_INVALID_ARG;

    memset(state, 0, sizeof(*state));
    state->status = OTA_STATUS_IDLE;

    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        OTA_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;

    if (err != ESP_OK)
        return err;

    int32_t update_id = 0;

    err = nvs_get_i32(
        handle,
        "update_id",
        &update_id
    );

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return ESP_OK;
    }

    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    size_t len = sizeof(state->target_version);

    err = nvs_get_str(
        handle,
        "target_ver",
        state->target_version,
        &len
    );

    if (err != ESP_OK)
    {
        nvs_close(handle);

        ESP_LOGW(
            TAG,
            "Incomplete OTA state found; clearing it"
        );

        ota_state_clear();
        return ESP_OK;
    }

    int32_t stored_status = (int32_t)OTA_STATUS_INSTALLING;

    err = nvs_get_i32(
        handle,
        "status",
        &stored_status
    );

    bool migrate_status = false;

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        /*
         * Legacy state migration:
         * Older firmware saved OTA state only after installation,
         * just before reboot. Therefore the safest equivalent state
         * is INSTALLING.
         */
        stored_status = (int32_t)OTA_STATUS_INSTALLING;
        migrate_status = true;

        ESP_LOGW(
            TAG,
            "Legacy OTA state found; migrating status to installing"
        );
    }
    else if (err != ESP_OK || !ota_status_is_valid(stored_status))
    {
        /*
         * Old/incompatible or corrupted status. Existing update_id +
         * target_version indicate an OTA was already persisted by the
         * previous implementation, which happened after installation.
         */
        ESP_LOGW(
            TAG,
            "Invalid OTA status found; migrating status to installing"
        );

        stored_status = (int32_t)OTA_STATUS_INSTALLING;
        migrate_status = true;
    }

    nvs_close(handle);

    state->exists = true;
    state->update_id = (int)update_id;
    state->status = (ota_status_t)stored_status;

    if (migrate_status)
    {
        esp_err_t migrate_err =
            ota_state_update_status(state->status);

        if (migrate_err != ESP_OK)
        {
            ESP_LOGW(
                TAG,
                "Could not persist migrated OTA status: %s",
                esp_err_to_name(migrate_err)
            );
        }
    }

    return ESP_OK;
}


esp_err_t ota_state_update_status(
    ota_status_t status)
{
    if (!ota_status_is_valid((int32_t)status))
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        OTA_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (err != ESP_OK)
        return err;

    err = nvs_set_i32(
        handle,
        "status",
        (int32_t)status
    );

    if (err == ESP_OK)
        err = nvs_commit(handle);

    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "OTA state changed: %s",
            ota_status_to_string(status)
        );
    }

    return err;
}


esp_err_t ota_state_clear(void)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        OTA_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (err != ESP_OK)
        return err;

    err = nvs_erase_all(handle);

    if (err == ESP_OK)
        err = nvs_commit(handle);

    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "OTA state cleared"
        );
    }

    return err;
}
