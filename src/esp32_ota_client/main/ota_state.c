#include "ota_state.h"

#include <string.h>

#include "nvs.h"
#include "esp_log.h"

#define OTA_NAMESPACE "ota_state"

static const char *TAG = "ota_state";


esp_err_t ota_state_save(
    int update_id,
    const char *target_version)
{
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
        err = nvs_commit(handle);

    nvs_close(handle);

    return err;
}


esp_err_t ota_state_load(
    ota_state_t *state)
{
    if (state == NULL)
        return ESP_ERR_INVALID_ARG;

    memset(state, 0, sizeof(*state));

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

    int32_t update_id;

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

    size_t length =
        sizeof(state->target_version);

    err = nvs_get_str(
        handle,
        "target_ver",
        state->target_version,
        &length
    );

    nvs_close(handle);

    if (err != ESP_OK)
        return err;

    state->update_id = update_id;
    state->pending = true;

    return ESP_OK;
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

    ESP_LOGI(TAG, "Pending OTA state cleared");

    return err;
}