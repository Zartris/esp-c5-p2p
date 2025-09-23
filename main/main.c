#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ESP-C5-P2P";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C5 P2P Application Starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    ESP_LOGI(TAG, "ESP32-C5 P2P Application initialized successfully!");
    ESP_LOGI(TAG, "Free heap size: %ld bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF Version: %s", esp_get_idf_version());
    
    // Main application loop
    while (1) {
        ESP_LOGI(TAG, "ESP32-C5 is running... Free heap: %ld bytes", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay for 5 seconds
    }
}