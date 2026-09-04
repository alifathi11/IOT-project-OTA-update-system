#include "api_client.h"
#include "api_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"

#define API_MAX_RETRIES       5
#define API_RETRY_DELAY_MS    2000
#define API_TIMEOUT_MS       10000

#define RESPONSE_BUFFER_SIZE 1024

static const char *TAG = "api_client";

typedef struct
{
    char data[RESPONSE_BUFFER_SIZE];
    size_t length;
} http_response_t;

static esp_err_t parse_update_response(
    const char *json,
    ota_update_info_t *update_info)
{
    if (json == NULL || update_info == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(update_info, 0, sizeof(*update_info));

    cJSON *root = cJSON_Parse(json);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Invalid JSON response");
        return ESP_FAIL;
    }

    cJSON *available =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "update_available"
        );

    if (!cJSON_IsBool(available))
    {
        ESP_LOGE(
            TAG,
            "Missing update_available field"
        );

        cJSON_Delete(root);
        return ESP_FAIL;
    }

    update_info->update_available =
        cJSON_IsTrue(available);

    /*
     * No update available.
     * Nothing else needs to be parsed.
     */
    if (!update_info->update_available)
    {
        cJSON_Delete(root);
        return ESP_OK;
    }

    cJSON *update_id =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "update_id"
        );

    cJSON *firmware_id =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "firmware_id"
        );

    cJSON *target_version =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "target_version"
        );

    cJSON *file_url =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "file_url"
        );

    cJSON *file_size =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "file_size"
        );

    cJSON *sha256 =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "sha256"
        );

    if (!cJSON_IsNumber(update_id) ||
        !cJSON_IsNumber(firmware_id) ||
        !cJSON_IsString(target_version) ||
        !cJSON_IsString(file_url) ||
        !cJSON_IsNumber(file_size) ||
        !cJSON_IsString(sha256))
    {
        ESP_LOGE(
            TAG,
            "Incomplete update response"
        );

        cJSON_Delete(root);
        return ESP_FAIL;
    }

    update_info->update_id =
        update_id->valueint;

    update_info->firmware_id =
        firmware_id->valueint;

    update_info->file_size =
        (size_t)file_size->valuedouble;

    snprintf(
        update_info->target_version,
        sizeof(update_info->target_version),
        "%s",
        target_version->valuestring
    );

    snprintf(
        update_info->file_url,
        sizeof(update_info->file_url),
        "%s",
        file_url->valuestring
    );

    snprintf(
        update_info->sha256,
        sizeof(update_info->sha256),
        "%s",
        sha256->valuestring
    );

    cJSON_Delete(root);

    return ESP_OK;
}

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
    esp_err_t err = ESP_FAIL;

    for (int attempt = 1;
         attempt <= API_MAX_RETRIES;
         attempt++)
    {
        ESP_LOGI(
            TAG,
            "HTTP request attempt %d/%d",
            attempt,
            API_MAX_RETRIES
        );

        err = esp_http_client_perform(client);

        if (err == ESP_OK)
        {
            break;
        }

        ESP_LOGW(
            TAG,
            "HTTP request failed: %s",
            esp_err_to_name(err)
        );

        if (attempt < API_MAX_RETRIES)
        {
            vTaskDelay(
                pdMS_TO_TICKS(API_RETRY_DELAY_MS)
            );
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Request failed after retries"
        );

        return err;
    }

    int status =
        esp_http_client_get_status_code(client);

    ESP_LOGI(
        TAG,
        "Response status: %d",
        status
    );

    if (response->length > 0)
    {
        ESP_LOGI(
            TAG,
            "Response body: %s",
            response->data
        );
    }

    if (status < 200 || status >= 300)
    {
        ESP_LOGE(
            TAG,
            "Server returned HTTP %d",
            status
        );

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
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = API_TIMEOUT_MS,
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
    const char *firmware_version,
    ota_update_info_t *update_info)
{
    if (update_info == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

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
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = API_TIMEOUT_MS,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to create HTTP client"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GET %s", url);

    esp_err_t err =
        perform_request(client, &response);

    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        return err;
    }

    return parse_update_response(
        response.data,
        update_info
    );
}

esp_err_t api_report_update_status(
    int update_id,
    const char *status,
    const char *message)
{
    char url[256];
    char body[512];
    http_response_t response = {0};

    snprintf(
        url,
        sizeof(url),
        "%s/api/updates/%d/status",
        SERVER_BASE_URL,
        update_id
    );

    snprintf(
        body,
        sizeof(body),
        "{\"status\":\"%s\",\"message\":\"%s\"}",
        status,
        message ? message : ""
    );

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = API_TIMEOUT_MS,
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

    ESP_LOGI(TAG, "Reporting OTA status: %s", status);

    esp_err_t err =
        perform_request(client, &response);

    esp_http_client_cleanup(client);

    return err;
}