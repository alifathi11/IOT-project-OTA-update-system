#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "api_client.h"
#include "wifi.h"
#include "ota_manager.h"
#include "ota_state.h"

#define DEVICE_ID_SIZE 32

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
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));

    char device_id[DEVICE_ID_SIZE];

    snprintf(device_id,
             sizeof(device_id),
             "ESP32-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);


    const esp_app_desc_t *app_desc =
        esp_app_get_description();

    const char *firmware_version =
        app_desc->version;


    ota_state_t ota_state;

    ESP_ERROR_CHECK(
        ota_state_load(&ota_state)
    );

    if (strcmp(firmware_version, "1.29.0") == 0)
    {
        ESP_LOGE(TAG,
                "TEST FAILURE: firmware validation failed");


        if (ota_state.pending)
        {
            if (wifi_connect() == ESP_OK)
            {
                api_report_update_status(
                    ota_state.update_id,
                    "failed",
                    "Firmware self-test failed"
                );

                ota_state_clear();
            }
        }


        esp_ota_mark_app_invalid_rollback_and_reboot();

        return;
    }

    /*
     * Normal boot validation:
     * A firmware remains pending until it passes
     * application validation.
     */

    if (ota_state.pending &&
        strcmp(firmware_version,
               ota_state.target_version) == 0)
    {
        ESP_LOGI(TAG,
                 "New firmware booted successfully");


        esp_err_t valid_err =
            esp_ota_mark_app_valid_cancel_rollback();


        if (valid_err != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "Failed to mark firmware valid: %s",
                     esp_err_to_name(valid_err));
        }


        if (wifi_connect() == ESP_OK)
        {
            if (api_report_update_status(
                    ota_state.update_id,
                    "success",
                    "New firmware booted successfully") == ESP_OK)
            {
                ota_state_clear();
            }
        }

        return;
    }



    if (wifi_connect() != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "WiFi connection failed");
        return;
    }


    api_register_device(
        device_id,
        firmware_version
    );


    ota_update_info_t update_info = {0};


    if (api_check_for_update(
            device_id,
            firmware_version,
            &update_info) != ESP_OK)
    {
        return;
    }


    if (!update_info.update_available)
    {
        return;
    }


    api_report_update_status(
        update_info.update_id,
        "downloading",
        "Downloading firmware"
    );


    esp_err_t ota_err =
        ota_install_from_url(
            update_info.file_url,
            update_info.sha256
        );


    if (ota_err != ESP_OK)
    {
        api_report_update_status(
            update_info.update_id,
            "failed",
            "OTA installation failed"
        );

        return;
    }


    ESP_ERROR_CHECK(
        ota_state_save(
            update_info.update_id,
            update_info.target_version
        )
    );


    api_report_update_status(
        update_info.update_id,
        "installing",
        "Firmware installed, rebooting"
    );


    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_restart();
}
