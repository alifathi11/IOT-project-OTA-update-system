#include "wifi.h"
#include "wifi_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1
#define WIFI_CONNECT_TIMEOUT_MS 20000

static const char *TAG = "wifi";

static EventGroupHandle_t wifi_event_group = NULL;
static esp_netif_t *wifi_sta_netif = NULL;
static int retry_count = 0;
static bool wifi_initialized = false;


static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        if (retry_count < WIFI_MAX_RETRY)
        {
            retry_count++;

            ESP_LOGI(
                TAG,
                "Connection failed. Retrying... (%d/%d)",
                retry_count,
                WIFI_MAX_RETRY
            );

            esp_wifi_connect();
        }
        else
        {
            ESP_LOGW(
                TAG,
                "WiFi reconnect attempts exhausted"
            );

            xEventGroupSetBits(
                wifi_event_group,
                WIFI_FAILED_BIT
            );
        }
    }

    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            TAG,
            "Connected. IP: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        retry_count = 0;

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_FAILED_BIT
        );

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}


static esp_err_t wifi_init_once(void)
{
    if (wifi_initialized)
        return ESP_OK;

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL)
        return ESP_ERR_NO_MEM;

    esp_err_t err = esp_netif_init();

    if (err != ESP_OK)
        return err;

    err = esp_event_loop_create_default();

    if (err != ESP_OK)
        return err;

    wifi_sta_netif =
        esp_netif_create_default_wifi_sta();

    if (wifi_sta_netif == NULL)
        return ESP_FAIL;

    wifi_init_config_t wifi_init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&wifi_init_config);

    if (err != ESP_OK)
        return err;

    err = esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL
    );

    if (err != ESP_OK)
        return err;

    err = esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL
    );

    if (err != ESP_OK)
        return err;

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err != ESP_OK)
        return err;

    err = esp_wifi_set_config(
        WIFI_IF_STA,
        &wifi_config
    );

    if (err != ESP_OK)
        return err;

    err = esp_wifi_start();

    if (err != ESP_OK)
        return err;

    err = esp_wifi_set_ps(WIFI_PS_NONE);

    if (err != ESP_OK)
        return err;

    wifi_initialized = true;

    return ESP_OK;
}


bool wifi_is_connected(void)
{
    if (!wifi_initialized ||
        wifi_event_group == NULL)
    {
        return false;
    }

    EventBits_t bits =
        xEventGroupGetBits(wifi_event_group);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}


esp_err_t wifi_connect(void)
{
    bool just_initialized = !wifi_initialized;

    esp_err_t err = wifi_init_once();

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "WiFi initialization failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    if (wifi_is_connected())
        return ESP_OK;

    retry_count = 0;

    xEventGroupClearBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT
    );

    /*
     * On the first initialization, WIFI_EVENT_STA_START performs the
     * first connection attempt. On later calls, explicitly reconnect.
     */
    if (!just_initialized)
    {
        err = esp_wifi_connect();

        if (err != ESP_OK)
        {
            ESP_LOGW(
                TAG,
                "Could not start WiFi reconnect: %s",
                esp_err_to_name(err)
            );

            return err;
        }
    }

    ESP_LOGI(
        TAG,
        "Connecting to %s...",
        WIFI_SSID
    );

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT)
        return ESP_OK;

    if (bits & WIFI_FAILED_BIT)
        return ESP_FAIL;

    ESP_LOGW(
        TAG,
        "WiFi connection timed out after %d ms",
        WIFI_CONNECT_TIMEOUT_MS
    );

    return ESP_ERR_TIMEOUT;
}
