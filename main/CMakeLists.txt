idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_http_server
        espressif__esp32-camera
        espressif__esp-sr
        nvs_flash
        esp_netif
        esp_event
    PRIV_REQUIRES
        hardware_driver
)
