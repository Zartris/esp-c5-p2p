idf_component_register(SRCS 
                        "main.cpp"
                        "wifi_connect.c"
                       REQUIRES esp_timer esp_event esp_netif nvs_flash esp_wifi
)