#include "app_manager.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "api_client.h"
#include "ota_manager.h"
#include "ota_state.h"
#include "ota_types.h"
#include "ota_validator.h"
#include "wifi.h"

#define DEVICE_ID_SIZE 32

static const char *TAG = "app_manager";


static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}


static void get_device_id(
    char *device_id,
    size_t size)
{
    uint8_t mac[6];

    ESP_ERROR_CHECK(
        esp_efuse_mac_get_default(mac)
    );

    snprintf(
        device_id,
        size,
        "ESP32-%02X%02X%02X%02X%02X%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );
}


static esp_err_t ensure_wifi(
    bool *wifi_ready)
{
    if (wifi_ready == NULL)
        return ESP_ERR_INVALID_ARG;

    if (*wifi_ready)
        return ESP_OK;

    esp_err_t err = wifi_connect();

    if (err == ESP_OK)
    {
        *wifi_ready = true;
        ESP_LOGI(TAG, "WiFi ready");
    }
    else
    {
        ESP_LOGE(TAG, "WiFi connection failed");
    }

    return err;
}


static esp_err_t report_terminal_status(
    ota_state_t *state,
    bool *wifi_ready,
    const char *status,
    const char *message)
{
    if (state == NULL ||
        wifi_ready == NULL ||
        status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_wifi(wifi_ready);

    if (err != ESP_OK)
        return err;

    err = api_report_update_status(
        state->update_id,
        status,
        message
    );

    if (err != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Could not report terminal OTA status '%s'; "
            "state will be retried on next boot",
            status
        );

        return err;
    }

    return ota_state_clear();
}


static bool partition_is_factory(
    const esp_partition_t *partition)
{
    return partition != NULL &&
           partition->type == ESP_PARTITION_TYPE_APP &&
           partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY;
}


static bool partition_is_ota(
    const esp_partition_t *partition)
{
    if (partition == NULL ||
        partition->type != ESP_PARTITION_TYPE_APP)
    {
        return false;
    }

    return partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
           partition->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
}


static esp_err_t handle_existing_ota_state(
    ota_state_t *state,
    const char *firmware_version,
    bool *wifi_ready)
{
    if (state == NULL ||
        firmware_version == NULL ||
        wifi_ready == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!state->exists)
        return ESP_OK;


    const esp_partition_t *running =
        esp_ota_get_running_partition();

    const bool running_factory =
        partition_is_factory(running);

    const bool running_ota =
        partition_is_ota(running);

    const bool target_is_running =
        strcmp(
            firmware_version,
            state->target_version
        ) == 0;


    esp_ota_img_states_t image_state =
        ESP_OTA_IMG_UNDEFINED;

    esp_err_t image_state_err =
        ESP_ERR_NOT_SUPPORTED;

    if (running != NULL)
    {
        image_state_err =
            esp_ota_get_state_partition(
                running,
                &image_state
            );
    }


    ESP_LOGI(
        TAG,
        "OTA state loaded: update_id=%d status=%s "
        "target=%s current=%s partition=%s",
        state->update_id,
        ota_status_to_string(state->status),
        state->target_version,
        firmware_version,
        running != NULL ? running->label : "unknown"
    );


    /*
     * Development/manual-flash recovery:
     *
     * idf.py flash writes the application into the factory partition.
     * NVS is not erased, therefore an old OTA state can survive.
     *
     * If the factory image has exactly the stored target version, this
     * is not an OTA validation boot. Factory partitions do not have an
     * OTA rollback state and must never be passed to
     * esp_ota_mark_app_valid_cancel_rollback().
     */
    if (running_factory && target_is_running)
    {
        ESP_LOGW(
            TAG,
            "Stale OTA state detected after manual factory flash; "
            "clearing stored OTA state"
        );

        return ota_state_clear();
    }


    /*
     * Retry terminal reports that could not reach the server earlier.
     */
    if (state->status == OTA_STATUS_ROLLED_BACK)
    {
        return report_terminal_status(
            state,
            wifi_ready,
            "rolled_back",
            "Firmware validation failed; previous firmware restored"
        );
    }

    if (state->status == OTA_STATUS_FAILED)
    {
        return report_terminal_status(
            state,
            wifi_ready,
            "failed",
            "OTA update failed"
        );
    }

    if (state->status == OTA_STATUS_SUCCESS)
    {
        if (target_is_running)
        {
            return report_terminal_status(
                state,
                wifi_ready,
                "success",
                "New firmware booted successfully"
            );
        }

        ESP_LOGW(
            TAG,
            "Stale success state does not match running firmware; "
            "clearing it"
        );

        return ota_state_clear();
    }


    /*
     * A real first boot of a rollback-enabled OTA image must be:
     *   - running from ota_0 / ota_1
     *   - the stored target version
     *   - ESP_OTA_IMG_PENDING_VERIFY
     *
     * Only in this case is mark-valid / mark-invalid legal.
     */
    if (target_is_running &&
        running_ota &&
        image_state_err == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        ESP_ERROR_CHECK(
            ota_state_update_status(
                OTA_STATUS_VALIDATING
            )
        );

        ESP_LOGI(
            TAG,
            "Validating newly installed OTA firmware"
        );


        if (!ota_validate_firmware(
                firmware_version))
        {
            ESP_LOGE(
                TAG,
                "Firmware validation failed; rollback requested"
            );

            ESP_ERROR_CHECK(
                ota_state_update_status(
                    OTA_STATUS_ROLLED_BACK
                )
            );

            esp_err_t rollback_err =
                esp_ota_mark_app_invalid_rollback_and_reboot();

            /*
             * Normally unreachable because successful rollback reboots.
             */
            ESP_LOGE(
                TAG,
                "Rollback could not be started: %s",
                esp_err_to_name(rollback_err)
            );

            return rollback_err;
        }


        esp_err_t valid_err =
            esp_ota_mark_app_valid_cancel_rollback();

        if (valid_err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Failed to mark OTA firmware valid: %s",
                esp_err_to_name(valid_err)
            );

            return valid_err;
        }


        ESP_ERROR_CHECK(
            ota_state_update_status(
                OTA_STATUS_SUCCESS
            )
        );

        state->status = OTA_STATUS_SUCCESS;

        ESP_LOGI(
            TAG,
            "New OTA firmware validated successfully"
        );

        return report_terminal_status(
            state,
            wifi_ready,
            "success",
            "New firmware booted and validated successfully"
        );
    }


    /*
     * If the target is running from an OTA slot but is already VALID
     * (for example, validation succeeded and reset happened before the
     * server report), finish the success report without calling
     * mark_app_valid again.
     */
    if (target_is_running &&
        running_ota &&
        image_state_err == ESP_OK &&
        image_state == ESP_OTA_IMG_VALID)
    {
        ESP_LOGI(
            TAG,
            "Target OTA image is already valid; completing success report"
        );

        ESP_ERROR_CHECK(
            ota_state_update_status(
                OTA_STATUS_SUCCESS
            )
        );

        state->status = OTA_STATUS_SUCCESS;

        return report_terminal_status(
            state,
            wifi_ready,
            "success",
            "Firmware was already validated successfully"
        );
    }


    /*
     * The stored target is not the running firmware.
     *
     * INSTALLING/VALIDATING means the device had moved far enough in the
     * OTA process that returning to the previous image is a rollback.
     */
    if (!target_is_running &&
        (state->status == OTA_STATUS_INSTALLING ||
         state->status == OTA_STATUS_VALIDATING))
    {
        ESP_LOGW(
            TAG,
            "Previous firmware is running after OTA; rollback detected"
        );

        ESP_ERROR_CHECK(
            ota_state_update_status(
                OTA_STATUS_ROLLED_BACK
            )
        );

        state->status = OTA_STATUS_ROLLED_BACK;

        return report_terminal_status(
            state,
            wifi_ready,
            "rolled_back",
            "Previous firmware restored after OTA validation failure"
        );
    }


    /*
     * Any earlier unfinished state is treated as an interrupted update.
     */
    ESP_LOGW(
        TAG,
        "Interrupted/stale OTA state detected: status=%s, "
        "partition=%s, image_state_err=%s",
        ota_status_to_string(state->status),
        running != NULL ? running->label : "unknown",
        esp_err_to_name(image_state_err)
    );

    ESP_ERROR_CHECK(
        ota_state_update_status(
            OTA_STATUS_FAILED
        )
    );

    state->status = OTA_STATUS_FAILED;

    return report_terminal_status(
        state,
        wifi_ready,
        "failed",
        "OTA update was interrupted before completion"
    );
}


static void perform_update_check(
    const char *device_id,
    const char *firmware_version)
{
    ota_update_info_t update_info = {0};

    ESP_LOGI(
        TAG,
        "Checking for firmware update"
    );

    esp_err_t err = api_check_for_update(
        device_id,
        firmware_version,
        &update_info
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Update check failed: %s",
            esp_err_to_name(err)
        );

        return;
    }

    if (!update_info.update_available)
    {
        ESP_LOGI(
            TAG,
            "No firmware update available"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Update available: %s -> %s (update_id=%d)",
        firmware_version,
        update_info.target_version,
        update_info.update_id
    );


    err = ota_state_save(
        update_info.update_id,
        update_info.target_version
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Could not persist OTA state: %s",
            esp_err_to_name(err)
        );

        return;
    }


    ESP_ERROR_CHECK(
        ota_state_update_status(
            OTA_STATUS_DOWNLOADING
        )
    );

    if (api_report_update_status(
            update_info.update_id,
            "downloading",
            "Downloading firmware from server") != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Could not report downloading status"
        );
    }


    esp_err_t ota_err =
        ota_install_from_url(
            update_info.file_url,
            update_info.sha256
        );


    if (ota_err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "OTA installation failed: %s",
            esp_err_to_name(ota_err)
        );

        ESP_ERROR_CHECK(
            ota_state_update_status(
                OTA_STATUS_FAILED
            )
        );

        if (api_report_update_status(
                update_info.update_id,
                "failed",
                "Firmware download, verification, or installation failed")
            == ESP_OK)
        {
            ESP_ERROR_CHECK(
                ota_state_clear()
            );
        }

        return;
    }


    /*
     * ota_install_from_url() returned successfully, therefore SHA-256
     * verification and boot-partition selection both succeeded.
     */
    ESP_ERROR_CHECK(
        ota_state_update_status(
            OTA_STATUS_VERIFIED
        )
    );

    if (api_report_update_status(
            update_info.update_id,
            "verified",
            "Firmware SHA-256 verification successful") != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Could not report verified status"
        );
    }


    ESP_ERROR_CHECK(
        ota_state_update_status(
            OTA_STATUS_INSTALLING
        )
    );

    if (api_report_update_status(
            update_info.update_id,
            "installing",
            "Firmware installed; rebooting into new partition") != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Could not report installing status"
        );
    }


    ESP_LOGI(
        TAG,
        "Restarting into new firmware"
    );

    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );

    esp_restart();
}


void app_manager_start(void)
{
    ESP_ERROR_CHECK(
        init_nvs()
    );


    char device_id[DEVICE_ID_SIZE];

    get_device_id(
        device_id,
        sizeof(device_id)
    );


    const esp_app_desc_t *app_desc =
        esp_app_get_description();

    const char *firmware_version =
        app_desc->version;


    ESP_LOGI(
        TAG,
        "Device ID: %s",
        device_id
    );

    ESP_LOGI(
        TAG,
        "Firmware version: %s",
        firmware_version
    );


    ota_state_t state = {0};

    ESP_ERROR_CHECK(
        ota_state_load(&state)
    );


    bool wifi_ready = false;


    if (state.exists)
    {
        esp_err_t state_err =
            handle_existing_ota_state(
                &state,
                firmware_version,
                &wifi_ready
            );

        if (state_err != ESP_OK)
        {
            ESP_LOGW(
                TAG,
                "Pending OTA state was not fully resolved; "
                "normal update check skipped"
            );

            return;
        }
    }


    if (ensure_wifi(&wifi_ready) != ESP_OK)
        return;


    esp_err_t register_err =
        api_register_device(
            device_id,
            firmware_version
        );

    if (register_err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Device registration failed: %s",
            esp_err_to_name(register_err)
        );

        return;
    }


    perform_update_check(
        device_id,
        firmware_version
    );
}
