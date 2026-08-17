#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "api_client.h"
#include "wifi.h"

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
    snprintf(
        device_id,
        sizeof(device_id),
        "ESP32-%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );

    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *firmware_version = app_desc->version;

    ESP_LOGI(TAG, "Device ID: %s", device_id);
    ESP_LOGI(TAG, "Firmware Version: %s", firmware_version);

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    if (wifi_connect() != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi connection failed.");
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi ready.");

    ESP_LOGI(TAG, "Registering device on server...");
    if (api_register_device(device_id, firmware_version) != ESP_OK)
    {
        ESP_LOGE(TAG, "Device registration failed.");
        return;
    }

    ESP_LOGI(TAG, "Device registered successfully.");

    ESP_LOGI(TAG, "Checking for update...");
    if (api_check_for_update(device_id, firmware_version) != ESP_OK)
    {
        ESP_LOGE(TAG, "Update check failed.");
        return;
    }

    ESP_LOGI(TAG, "Week-1 ESP32 flow completed successfully.");
}
