idf_component_register(
    SRCS
        "grok.c"
        "grok_text.c"
        "grok_pick.c"
        "grok_wifi.c"
        "grok_http.c"
        "grok_https.c"
        "grok_ble.c"
        "grok_ui.c"
        "grok_console.c"
        "grok_ota_util.c"
        "grok_update.c"
        "grok_web_util.c"
        "grok_web.c"
    INCLUDE_DIRS "."
    REQUIRES
        osd common button tab console nvs_flash freertos
        esp_wifi esp_netif esp_event esp_http_client esp_http_server esp_timer mbedtls
        esp_driver_uart vfs app_update esp_partition esp_app_format
        bt lvgl
)
