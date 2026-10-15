idf_component_register(
    SRCS
        "grok.c"
        "grok_text.c"
        "grok_wifi.c"
        "grok_http.c"
        "grok_ble.c"
        "grok_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        osd common button tab console nvs_flash freertos
        esp_wifi esp_netif esp_event esp_http_client
        bt lvgl
)
