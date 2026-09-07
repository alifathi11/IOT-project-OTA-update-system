idf_component_register(
    SRCS
        "main.c"
        "app_manager.c"
        "ota_validator.c"
        "wifi.c"
        "api_client.c"
        "ota_manager.c"
        "ota_state.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_app_format
        esp_hw_support
        esp_http_client
        app_update
        mbedtls
)
