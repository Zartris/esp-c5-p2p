idf_component_register(SRCS
                        "main.cpp"
                        "esp_now_manager.cpp"
                        "test_framework.cpp"
                        "performance_tests.cpp"

                       REQUIRES esp_timer esp_event esp_netif nvs_flash esp_wifi esp_now
)