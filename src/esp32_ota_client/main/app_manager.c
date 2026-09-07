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
#define FIRMWARE_VERSION_SIZE 32
#define SERVICE_TASK_STACK_SIZE 12288
#define SERVICE_TASK_PRIORITY 5
#define SERVICE_STACK_LOG_EVERY_CYCLES 10

typedef struct
{
    char device_id[DEVICE_ID_SIZE];
    char firmware_version[FIRMWARE_VERSION_SIZE];
} service_task_context_t;

static service_task_context_t s_service_context;
static TaskHandle_t s_service_task_handle = NULL;

/*
 * Defensive guard against overlapping update attempts.
 * Today all periodic work runs in one service task, but keeping this guard
 * makes the OTA path safe if more service work is added later.
 */
static bool s_ota_in_progress = false;

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

    if (wifi_is_connected())
    {
        *wifi_ready = true;
        return ESP_OK;
    }

    *wifi_ready = false;

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


static void perform_update_check(
    const char *device_id,
    const char *firmware_version);

static esp_err_t report_terminal_status(
    ota_state_t *state,
    bool *wifi_ready,
    const char *status,
    const char *message);

static esp_err_t retry_terminal_ota_report_if_needed(
    bool *wifi_ready,
    bool *terminal_state_present);

static void run_service_loop(
    const char *device_id,
    const char *firmware_version)
{
    const TickType_t heartbeat_delay =
        pdMS_TO_TICKS(30000);

    const TickType_t update_check_interval =
        pdMS_TO_TICKS(60000);

    TickType_t last_update_check =
        xTaskGetTickCount();

    unsigned cycle_count = 0;

    ESP_LOGI(
        TAG,
        "Service loop started (heartbeat=30s, update-check=60s)"
    );

    while (true)
    {
        vTaskDelay(heartbeat_delay);
        cycle_count++;

        if (!wifi_is_connected())
        {
            ESP_LOGW(
                TAG,
                "WiFi disconnected; reconnecting"
            );

            if (wifi_connect() != ESP_OK)
            {
                ESP_LOGW(
                    TAG,
                    "Service cycle skipped: WiFi reconnect failed"
                );

                if ((cycle_count %
                     SERVICE_STACK_LOG_EVERY_CYCLES) == 0)
                {
                    ESP_LOGI(
                        TAG,
                        "Service task stack watermark: %u",
                        (unsigned)uxTaskGetStackHighWaterMark(NULL)
                    );
                }

                continue;
            }
        }


        /*
         * If an earlier OTA reached a terminal state but its server report
         * failed because of a temporary network problem, retry that report
         * before doing any new work. This prevents a second OTA job from
         * starting while the previous one is still unresolved locally.
         */
        bool terminal_state_present = false;

        esp_err_t terminal_err =
            retry_terminal_ota_report_if_needed(
                NULL,
                &terminal_state_present
            );

        if (terminal_state_present)
        {
            if (terminal_err != ESP_OK)
            {
                ESP_LOGW(
                    TAG,
                    "Pending OTA terminal report still unresolved; "
                    "new update checks remain paused"
                );
            }

            if ((cycle_count %
                 SERVICE_STACK_LOG_EVERY_CYCLES) == 0)
            {
                ESP_LOGI(
                    TAG,
                    "Service task stack watermark: %u",
                    (unsigned)uxTaskGetStackHighWaterMark(NULL)
                );
            }

            continue;
        }


        if (!s_ota_in_progress)
        {
            ESP_LOGI(TAG, "Sending heartbeat");

            esp_err_t heartbeat_err =
                api_register_device(
                    device_id,
                    firmware_version
                );

            if (heartbeat_err == ESP_OK)
            {
                ESP_LOGI(TAG, "Heartbeat successful");
            }
            else
            {
                ESP_LOGW(
                    TAG,
                    "Heartbeat failed: %s",
                    esp_err_to_name(heartbeat_err)
                );
            }
        }
        else
        {
            ESP_LOGI(
                TAG,
                "Heartbeat skipped while OTA is active"
            );
        }


        TickType_t now =
            xTaskGetTickCount();

        if ((now - last_update_check) >=
            update_check_interval)
        {
            last_update_check = now;

            if (s_ota_in_progress)
            {
                ESP_LOGI(
                    TAG,
                    "Periodic update check skipped: OTA already active"
                );
            }
            else if (!wifi_is_connected())
            {
                ESP_LOGW(
                    TAG,
                    "Periodic update check skipped: WiFi disconnected"
                );
            }
            else
            {
                ESP_LOGI(
                    TAG,
                    "Periodic update check"
                );

                perform_update_check(
                    device_id,
                    firmware_version
                );
            }
        }


        if ((cycle_count %
             SERVICE_STACK_LOG_EVERY_CYCLES) == 0)
        {
            ESP_LOGI(
                TAG,
                "Service task stack watermark: %u",
                (unsigned)uxTaskGetStackHighWaterMark(NULL)
            );
        }
    }
}

static void service_task(void *arg)
{
    service_task_context_t *context =
        (service_task_context_t *)arg;

    ESP_LOGI(
        TAG,
        "Service task started (stack=%d bytes)",
        SERVICE_TASK_STACK_SIZE
    );

    run_service_loop(
        context->device_id,
        context->firmware_version
    );

    /*
     * run_service_loop() is intentionally infinite.
     * This is only a defensive fallback.
     */
    s_service_task_handle = NULL;
    vTaskDelete(NULL);
}


static esp_err_t start_service_task(
    const char *device_id,
    const char *firmware_version)
{
    if (device_id == NULL ||
        firmware_version == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_service_task_handle != NULL)
    {
        ESP_LOGW(
            TAG,
            "Service task already running"
        );

        return ESP_OK;
    }

    snprintf(
        s_service_context.device_id,
        sizeof(s_service_context.device_id),
        "%s",
        device_id
    );

    snprintf(
        s_service_context.firmware_version,
        sizeof(s_service_context.firmware_version),
        "%s",
        firmware_version
    );

    BaseType_t created =
        xTaskCreate(
            service_task,
            "ota_service",
            SERVICE_TASK_STACK_SIZE,
            &s_service_context,
            SERVICE_TASK_PRIORITY,
            &s_service_task_handle
        );

    if (created != pdPASS)
    {
        s_service_task_handle = NULL;

        ESP_LOGE(
            TAG,
            "Failed to create service task"
        );

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
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


static bool ota_status_is_terminal(
    ota_status_t status)
{
    return status == OTA_STATUS_SUCCESS ||
           status == OTA_STATUS_FAILED ||
           status == OTA_STATUS_ROLLED_BACK;
}


static esp_err_t retry_terminal_ota_report_if_needed(
    bool *wifi_ready,
    bool *terminal_state_present)
{
    if (terminal_state_present == NULL)
        return ESP_ERR_INVALID_ARG;

    *terminal_state_present = false;

    ota_state_t state = {0};

    esp_err_t err =
        ota_state_load(&state);

    if (err != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Could not inspect OTA state: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    if (!state.exists ||
        !ota_status_is_terminal(state.status))
    {
        return ESP_OK;
    }

    *terminal_state_present = true;

    bool local_wifi_ready =
        wifi_is_connected();

    bool *ready_ptr =
        wifi_ready != NULL
            ? wifi_ready
            : &local_wifi_ready;

    ESP_LOGW(
        TAG,
        "Retrying unresolved OTA terminal report: %s",
        ota_status_to_string(state.status)
    );

    switch (state.status)
    {
        case OTA_STATUS_SUCCESS:
            return report_terminal_status(
                &state,
                ready_ptr,
                "success",
                "Firmware update completed successfully"
            );

        case OTA_STATUS_ROLLED_BACK:
            return report_terminal_status(
                &state,
                ready_ptr,
                "rolled_back",
                "Firmware validation failed; previous firmware restored"
            );

        case OTA_STATUS_FAILED:
            return report_terminal_status(
                &state,
                ready_ptr,
                "failed",
                "OTA update failed"
            );

        default:
            return ESP_OK;
    }
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
    if (s_ota_in_progress)
    {
        ESP_LOGW(
            TAG,
            "Update check skipped: OTA already in progress"
        );
        return;
    }

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


    /*
     * From this point until failure cleanup or reboot, no second OTA may
     * be started and service work should not overlap this update.
     */
    s_ota_in_progress = true;


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

        s_ota_in_progress = false;
        return;
    }


    err = ota_state_update_status(
        OTA_STATUS_DOWNLOADING
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Could not persist downloading state: %s",
            esp_err_to_name(err)
        );

        s_ota_in_progress = false;
        return;
    }


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

        esp_err_t state_err =
            ota_state_update_status(
                OTA_STATUS_FAILED
            );

        if (state_err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Could not persist failed OTA state: %s",
                esp_err_to_name(state_err)
            );
        }

        if (api_report_update_status(
                update_info.update_id,
                "failed",
                "Firmware download, verification, or installation failed")
            == ESP_OK)
        {
            esp_err_t clear_err =
                ota_state_clear();

            if (clear_err != ESP_OK)
            {
                ESP_LOGW(
                    TAG,
                    "Could not clear failed OTA state: %s",
                    esp_err_to_name(clear_err)
                );
            }
        }
        else
        {
            ESP_LOGW(
                TAG,
                "Failed OTA status could not be reported; "
                "service loop will retry it"
            );
        }

        s_ota_in_progress = false;
        return;
    }


    /*
     * ota_install_from_url() returned successfully, therefore SHA-256
     * verification and boot-partition selection both succeeded.
     */
    err = ota_state_update_status(
        OTA_STATUS_VERIFIED
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Could not persist verified state: %s",
            esp_err_to_name(err)
        );

        s_ota_in_progress = false;
        return;
    }


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


    err = ota_state_update_status(
        OTA_STATUS_INSTALLING
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Could not persist installing state: %s",
            esp_err_to_name(err)
        );

        s_ota_in_progress = false;
        return;
    }


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

    /*
     * Successful OTA never returns to the service loop. The new firmware
     * starts with a fresh service task after reboot.
     */
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
            if (ota_status_is_terminal(state.status))
            {
                ESP_LOGW(
                    TAG,
                    "OTA terminal report is pending; "
                    "service task will retry it"
                );

                ESP_ERROR_CHECK(
                    start_service_task(
                        device_id,
                        firmware_version
                    )
                );

                return;
            }

            ESP_LOGW(
                TAG,
                "Pending OTA state was not fully resolved; "
                "normal update check skipped"
            );

            return;
        }
    }


    if (ensure_wifi(&wifi_ready) != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Initial WiFi connection failed; heartbeat loop will retry"
        );

        ESP_ERROR_CHECK(
            start_service_task(
                device_id,
                firmware_version
            )
        );

        return;
    }


    esp_err_t register_err =
        api_register_device(
            device_id,
            firmware_version
        );

    if (register_err != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Initial device registration failed: %s; "
            "heartbeat loop will retry",
            esp_err_to_name(register_err)
        );

        ESP_ERROR_CHECK(
            start_service_task(
                device_id,
                firmware_version
            )
        );

        return;
    }


    perform_update_check(
        device_id,
        firmware_version
    );


    /*
     * Stage 3.2.1:
     * Run heartbeat + periodic OTA checks in a dedicated task.
     *
     * The previous version kept the infinite service loop on ESP-IDF's
     * main task. A periodic OTA call then nested the HTTP/OTA stack under
     * that loop and overflowed the main-task stack.
     */
    ESP_ERROR_CHECK(
        start_service_task(
            device_id,
            firmware_version
        )
    );
}
