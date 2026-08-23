#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "api_client.h"
#include "wifi.h"
#include "ota_manager.h"
#include "ota_state.h"
#include "esp_ota_ops.h"

#define DEVICE_ID_SIZE 32

#define API_MAX_RETRIES 5
#define API_RETRY_DELAY_MS 2000

static const char *TAG = "main";


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);


    uint8_t mac[6];

    ESP_ERROR_CHECK(
        esp_efuse_mac_get_default(mac)
    );


    char device_id[DEVICE_ID_SIZE];

    snprintf(
        device_id,
        sizeof(device_id),
        "ESP32-%02X%02X%02X%02X%02X%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );


    const esp_app_desc_t *app_desc =
        esp_app_get_description();

    const char *firmware_version =
        app_desc->version;


    ota_state_t ota_state;

    ESP_ERROR_CHECK(
        ota_state_load(&ota_state)
    );


    bool ota_just_completed = false;


    ESP_LOGI(
        TAG,
        "Device ID: %s",
        device_id
    );

    ESP_LOGI(
        TAG,
        "Firmware Version: %s",
        firmware_version
    );


    ESP_ERROR_CHECK(
        ota_print_partition_info()
    );


    ESP_LOGI(
        TAG,
        "Connecting to Wi-Fi..."
    );


    if (wifi_connect() != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Wi-Fi connection failed."
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Wi-Fi ready."
    );



    /*
     * Register device first
     */

    ESP_LOGI(
        TAG,
        "Registering device on server..."
    );


    if (api_register_device(
            device_id,
            firmware_version) != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Device registration failed."
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Device registered successfully."
    );



    /*
     * Check previous OTA result
     */

    if (ota_state.pending)
    {
        ESP_LOGI(
            TAG,
            "Pending OTA found: update #%d -> %s",
            ota_state.update_id,
            ota_state.target_version
        );


        if (strcmp(
                firmware_version,
                ota_state.target_version) == 0)
        {
            ESP_LOGI(
                TAG,
                "New firmware booted successfully"
            );


            esp_err_t err =
                api_report_update_status(
                    ota_state.update_id,
                    "success",
                    "New firmware booted successfully"
                );


            if (err == ESP_OK)
            {
                ESP_ERROR_CHECK(
                    ota_state_clear()
                );

                ota_just_completed = true;
            }
            else
            {
                ESP_LOGW(
                    TAG,
                    "Could not report OTA success"
                );
            }
        }
    }



    /*
     * Avoid checking update again
     * immediately after successful OTA
     */

    if (ota_just_completed)
    {
        ESP_LOGI(
            TAG,
            "OTA completed successfully. Skipping update check."
        );

        return;
    }



    /*
     * Check new firmware
     */

    ESP_LOGI(
        TAG,
        "Checking for update..."
    );


    ota_update_info_t update_info = {0};


    if (api_check_for_update(
            device_id,
            firmware_version,
            &update_info) != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Update check failed."
        );

        return;
    }



    if (!update_info.update_available)
    {
        ESP_LOGI(
            TAG,
            "No firmware update available."
        );

        return;
    }



    ESP_LOGI(
        TAG,
        "Update available!"
    );


    ESP_LOGI(
        TAG,
        "Target version: %s",
        update_info.target_version
    );


    ESP_LOGI(
        TAG,
        "Firmware ID: %d",
        update_info.firmware_id
    );


    ESP_LOGI(
        TAG,
        "Firmware size: %u bytes",
        (unsigned int)update_info.file_size
    );


    ESP_LOGI(
        TAG,
        "Firmware URL: %s",
        update_info.file_url
    );


    ESP_LOGI(
        TAG,
        "SHA-256: %s",
        update_info.sha256
    );



    /*
     * Downloading state
     */

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



    ESP_LOGI(
        TAG,
        "Starting OTA update to version %s...",
        update_info.target_version
    );


    esp_err_t ota_err =
        ota_install_from_url(
            update_info.file_url
        );



    if (ota_err != ESP_OK)
    {
        api_report_update_status(
            update_info.update_id,
            "failed",
            "Firmware download or installation failed"
        );


        ESP_LOGE(
            TAG,
            "OTA update failed: %s",
            esp_err_to_name(ota_err)
        );

        return;
    }



    /*
     * Save OTA state before reboot
     */

    ESP_ERROR_CHECK(
        ota_state_save(
            update_info.update_id,
            update_info.target_version
        )
    );



    if (api_report_update_status(
            update_info.update_id,
            "installing",
            "Firmware installed, rebooting into new partition")
        != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Could not report installing status"
        );
    }

    const esp_partition_t *running_partition =
    esp_ota_get_running_partition();

    ESP_LOGI(
        TAG,
        "Current running partition: %s",
        running_partition->label
    );

    ESP_LOGI(
        TAG,
        "Restarting device..."
    );


    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );


    esp_restart();
}