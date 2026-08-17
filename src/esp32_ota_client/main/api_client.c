#include "api_client.h"
#include "api_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "api_client";


esp_err_t api_register_device(
    const char *device_id,
    const char *firmware_version)
{
    char url[256];

    snprintf(
        url,
        sizeof(url),
        "%s/api/devices/register",
        SERVER_BASE_URL
    );

    char body[256];

    snprintf(
        body,
        sizeof(body),
        "{\"device_id\":\"%s\",\"firmware_version\":\"%s\"}",
        device_id,
        firmware_version
    );

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_method(
        client,
        HTTP_METHOD_POST
    );

    esp_http_client_set_header(
        client,
        "Content-Type",
        "application/json"
    );

    esp_http_client_set_post_field(
        client,
        body,
        strlen(body)
    );

    ESP_LOGI(TAG, "POST %s", url);
    ESP_LOGI(TAG, "Body: %s", body);

    esp_err_t err =
        esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status =
            esp_http_client_get_status_code(client);

        ESP_LOGI(
            TAG,
            "Response status: %d",
            status
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Request failed: %s",
            esp_err_to_name(err)
        );
    }

    esp_http_client_cleanup(client);

    return err;
}


esp_err_t api_check_for_update(
    const char *device_id)
{
    char url[256];

    snprintf(
        url,
        sizeof(url),
        "%s/api/devices/%s/update",
        SERVER_BASE_URL,
        device_id
    );

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL)
    {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GET %s", url);

    esp_err_t err =
        esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status =
            esp_http_client_get_status_code(client);

        ESP_LOGI(
            TAG,
            "Response status: %d",
            status
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Request failed: %s",
            esp_err_to_name(err)
        );
    }

    esp_http_client_cleanup(client);

    return err;
}