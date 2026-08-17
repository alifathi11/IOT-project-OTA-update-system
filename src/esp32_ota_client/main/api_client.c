#include "api_client.h"
#include "api_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

#define RESPONSE_BUFFER_SIZE 1024

static const char *TAG = "api_client";

typedef struct
{
    char data[RESPONSE_BUFFER_SIZE];
    size_t length;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA ||
        event->user_data == NULL ||
        event->data == NULL ||
        event->data_len <= 0)
    {
        return ESP_OK;
    }

    http_response_t *response = (http_response_t *)event->user_data;
    size_t free_space = sizeof(response->data) - response->length - 1;
    size_t copy_size = (size_t)event->data_len;

    if (copy_size > free_space)
    {
        copy_size = free_space;
    }

    if (copy_size > 0)
    {
        memcpy(response->data + response->length, event->data, copy_size);
        response->length += copy_size;
        response->data[response->length] = '\0';
    }

    return ESP_OK;
}

static esp_err_t perform_request(
    esp_http_client_handle_t client,
    http_response_t *response)
{
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Request failed: %s", esp_err_to_name(err));
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Response status: %d", status);

    if (response->length > 0)
    {
        ESP_LOGI(TAG, "Response body: %s", response->data);
    }

    if (status < 200 || status >= 300)
    {
        ESP_LOGE(TAG, "Server returned HTTP %d", status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t api_register_device(
    const char *device_id,
    const char *firmware_version)
{
    char url[256];
    char body[256];
    http_response_t response = {0};

    snprintf(
        url,
        sizeof(url),
        "%s/api/devices/register",
        SERVER_BASE_URL
    );

    snprintf(
        body,
        sizeof(body),
        "{\"device_uid\":\"%s\",\"current_version\":\"%s\"}",
        device_id,
        firmware_version
    );

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
        .user_data = &response,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    ESP_LOGI(TAG, "POST %s", url);
    ESP_LOGI(TAG, "Body: %s", body);

    esp_err_t err = perform_request(client, &response);
    esp_http_client_cleanup(client);

    return err;
}

esp_err_t api_check_for_update(
    const char *device_id,
    const char *firmware_version)
{
    char url[320];
    http_response_t response = {0};

    snprintf(
        url,
        sizeof(url),
        "%s/api/devices/%s/update?version=%s",
        SERVER_BASE_URL,
        device_id,
        firmware_version
    );

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
        .user_data = &response,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GET %s", url);

    esp_err_t err = perform_request(client, &response);
    esp_http_client_cleanup(client);

    return err;
}
