#include <stdio.h>
#include <esp_event.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi_connect.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
  ESP_LOGI(TAG, "Welcome to the ESP32-C5 P2P Application!");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "Connecting to WiFi");

  connect_to_wifi();

  if (is_wifi_connected())
  {
    ESP_LOGI(TAG, "WiFi connected successfully!");
  }
  else
  {
    ESP_LOGW(TAG, "Not connected to WiFi.");
  }

  while (true) {
    ESP_LOGI(TAG, "ESP32-C5 P2P application running...");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}