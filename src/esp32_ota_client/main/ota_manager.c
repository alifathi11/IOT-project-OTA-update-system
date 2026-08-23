#include <stdlib.h>

#include "ota_manager.h"

#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "ota_manager";

#define OTA_BUFFER_SIZE 4096

esp_err_t ota_print_partition_info(void)
{
    const esp_partition_t *running =
        esp_ota_get_running_partition();

    if (running == NULL)
    {
        ESP_LOGE(TAG, "Could not get running partition");
        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Running partition: %s",
        running->label
    );

    ESP_LOGI(
        TAG,
        "Running partition address: 0x%lx",
        running->address
    );

    ESP_LOGI(
        TAG,
        "Running partition size: %lu bytes",
        running->size
    );

    const esp_partition_t *next =
        esp_ota_get_next_update_partition(NULL);

    if (next == NULL)
    {
        ESP_LOGE(TAG, "Could not find next OTA partition");
        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Next OTA partition: %s",
        next->label
    );

    ESP_LOGI(
        TAG,
        "Next OTA partition address: 0x%lx",
        next->address
    );

    ESP_LOGI(
        TAG,
        "Next OTA partition size: %lu bytes",
        next->size
    );

    return ESP_OK;
}

esp_err_t ota_install_from_url(const char *firmware_url)
{
    ESP_LOGI(TAG, "Starting OTA update");
    ESP_LOGI(TAG, "Firmware URL: %s", firmware_url);

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL)
    {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Target partition: %s",
        update_partition->label
    );

    esp_http_client_config_t config = {
        .url = firmware_url,
        .timeout_ms = 10000,
        .buffer_size = OTA_BUFFER_SIZE,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to open HTTP connection: %s",
            esp_err_to_name(err)
        );

        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_length =
        esp_http_client_fetch_headers(client);

    int status_code =
        esp_http_client_get_status_code(client);

    if (status_code != 200)
    {
        ESP_LOGE(
            TAG,
            "Server returned HTTP %d",
            status_code
        );

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return ESP_FAIL;
    }

    if (content_length > update_partition->size)
    {
        ESP_LOGE(
            TAG,
            "Firmware is too large: %lld bytes",
            content_length
        );

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(
        TAG,
        "Firmware size: %lld bytes",
        content_length
    );

    esp_ota_handle_t ota_handle;

    err = esp_ota_begin(
        update_partition,
        OTA_SIZE_UNKNOWN,
        &ota_handle
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_begin failed: %s",
            esp_err_to_name(err)
        );

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return err;
    }

    uint8_t *buffer = malloc(OTA_BUFFER_SIZE);

    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Could not allocate OTA buffer");

        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return ESP_ERR_NO_MEM;
    }

    size_t total_written = 0;

    while (1)
    {
        int read_len = esp_http_client_read(
            client,
            (char *)buffer,
            OTA_BUFFER_SIZE
        );

        if (read_len < 0)
        {
            ESP_LOGE(TAG, "HTTP read failed");

            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);

            return ESP_FAIL;
        }

        if (read_len == 0)
        {
            break;
        }

        err = esp_ota_write(
            ota_handle,
            buffer,
            read_len
        );

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "OTA write failed: %s",
                esp_err_to_name(err)
            );

            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);

            return err;
        }

        total_written += read_len;

        ESP_LOGI(
            TAG,
            "Written %u bytes",
            (unsigned int)total_written
        );
    }

    free(buffer);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    err = esp_ota_end(ota_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_end failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    err = esp_ota_set_boot_partition(
        update_partition
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to set boot partition: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    ESP_LOGI(
        TAG,
        "OTA completed successfully"
    );

    ESP_LOGI(
        TAG,
        "Next boot partition: %s",
        update_partition->label
    );

    return ESP_OK;
}