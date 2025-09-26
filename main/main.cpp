#include <stdio.h>
#include <esp_event.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "app.hpp"
#include "esp_now_manager.hpp"
#include "test_framework.hpp"
#include "performance_tests.hpp"

static const char *TAG = "main";

// Global instances
static ESPNowManager* esp_now_manager = nullptr;
static TestFramework* test_framework = nullptr;
static PerformanceTests* performance_tests = nullptr;

// Test configuration
static test_role_t current_role = TEST_ROLE_PEER;

// Discovery timing tracking
static uint64_t system_boot_time_us = 0;
static uint64_t discovery_start_time_us = 0;
static bool discovery_timing_active = false;

// Task handles for background operations
static TaskHandle_t discovery_task_handle = nullptr;
static TaskHandle_t peer_cleanup_task_handle = nullptr;

// ---- Background Tasks ----

// Continuous discovery task - handles dynamic peer discovery
static void continuous_discovery_task(void *arg) {
    (void)arg;

    const uint32_t DISCOVERY_INTERVAL_MS = 1000;      // Send discovery every 1 seconds
    const uint32_t DISCOVERY_BURST_COUNT = 3;         // Send 3 discovery packets in a burst
    const uint32_t DISCOVERY_BURST_INTERVAL_MS = 250; // 250ms between burst packets

    ESP_LOGI(TAG, "Continuous discovery task started");

    TickType_t last_discovery_time = 0;

    while (true) {
        TickType_t current_time = xTaskGetTickCount();

        // Check if it's time for a discovery cycle
        if ((current_time - last_discovery_time) >= pdMS_TO_TICKS(DISCOVERY_INTERVAL_MS)) {
            ESP_LOGD(TAG, "Starting discovery burst cycle");

            // Send discovery burst
            for (uint32_t i = 0; i < DISCOVERY_BURST_COUNT; i++) {
                esp_err_t ret = esp_now_manager->send_discovery_request();
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Discovery request failed: %s", esp_err_to_name(ret));
                }

                if (i < DISCOVERY_BURST_COUNT - 1) {
                    vTaskDelay(pdMS_TO_TICKS(DISCOVERY_BURST_INTERVAL_MS));
                }
            }

            last_discovery_time = current_time;
        }

        // Sleep for a short time before next check
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Peer cleanup task - removes stale peers that haven't been seen recently
static void peer_cleanup_task(void *arg) {
    (void)arg;

    const uint32_t CLEANUP_INTERVAL_MS = 30000;    // Check every 30 seconds
    const uint64_t PEER_TIMEOUT_US = 60000000;     // Remove peers not seen for 60 seconds

    ESP_LOGI(TAG, "Peer cleanup task started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CLEANUP_INTERVAL_MS));

        auto peers = esp_now_manager->get_peers();
        uint64_t current_time_us = esp_timer_get_time();

        for (const auto& peer : peers) {
            if ((current_time_us - peer.last_seen_us) > PEER_TIMEOUT_US) {
                ESP_LOGI(TAG, "Removing stale peer: %02x:%02x:%02x:%02x:%02x:%02x (last seen %.1f seconds ago)",
                         peer.mac_addr[0], peer.mac_addr[1], peer.mac_addr[2],
                         peer.mac_addr[3], peer.mac_addr[4], peer.mac_addr[5],
                         (current_time_us - peer.last_seen_us) / 1000000.0f);

                esp_now_manager->remove_peer(peer.mac_addr);
            }
        }

        ESP_LOGD(TAG, "Peer cleanup completed, %zu active peers", esp_now_manager->get_peer_count());
    }
}

// ---- Implementation of setup & loop ----
void setup() {
    system_boot_time_us = esp_timer_get_time();
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-C5 ESP-NOW Discovery Test Device");
    ESP_LOGI(TAG, "BOOT_TIMESTAMP: %llu us", system_boot_time_us);
    ESP_LOGI(TAG, "DEVICE_MAC: Will be shown after initialization");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing ESP-NOW Manager for 5GHz operation");

    // Initialize ESP-NOW Manager
    esp_now_manager = &ESPNowManager::get_instance();
    ret = esp_now_manager->initialize(36); // 5GHz channel 36
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW Manager: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "ESP-NOW Manager initialized successfully");
    ESP_LOGI(TAG, "DEVICE_MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             esp_now_manager->get_local_mac()[0], esp_now_manager->get_local_mac()[1],
             esp_now_manager->get_local_mac()[2], esp_now_manager->get_local_mac()[3],
             esp_now_manager->get_local_mac()[4], esp_now_manager->get_local_mac()[5]);

    // Initialize Test Framework
    test_configuration_t config = {};
    config.role = current_role;
    config.test_duration_ms = 30000;
    config.test_iterations = 1000;
    config.enable_logging = true;

    test_framework = new TestFramework();
    ret = test_framework->initialize(current_role, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Test Framework: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Test Framework initialized as %s",
             current_role == TEST_ROLE_COORDINATOR ? "COORDINATOR" :
             current_role == TEST_ROLE_PEER ? "PEER" : "OBSERVER");

    // Initialize Performance Tests
    performance_tests = new PerformanceTests(*test_framework, *esp_now_manager);

    // Set up callbacks
    esp_now_manager->set_peer_discovered_callback([](const esp_now_peer_info_t* peer) {
        uint64_t discovery_time_us = esp_timer_get_time();
        float time_since_boot_ms = (discovery_time_us - system_boot_time_us) / 1000.0f;
        float time_since_discovery_start_ms = discovery_timing_active ?
            (discovery_time_us - discovery_start_time_us) / 1000.0f : 0.0f;

        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "PEER_DISCOVERED!");
        ESP_LOGI(TAG, "PEER_MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 peer->mac_addr[0], peer->mac_addr[1], peer->mac_addr[2],
                 peer->mac_addr[3], peer->mac_addr[4], peer->mac_addr[5]);
        ESP_LOGI(TAG, "PEER_RSSI: %d dBm", peer->rssi);
        ESP_LOGI(TAG, "DISCOVERY_TIMESTAMP: %llu us", discovery_time_us);
        ESP_LOGI(TAG, "TIME_SINCE_BOOT: %.3f ms", time_since_boot_ms);
        if (discovery_timing_active) {
            ESP_LOGI(TAG, "DISCOVERY_LATENCY: %.3f ms", time_since_discovery_start_ms);
        }
        ESP_LOGI(TAG, "========================================");
    });

    test_framework->set_test_completed_callback([](const test_result_t& result) {
        ESP_LOGI(TAG, "Test completed: %s - %s",
                 result.test_name.c_str(),
                 result.status == TEST_STATUS_COMPLETED ? "PASSED" : "FAILED");
    });

    // Start background tasks for continuous operations
    ESP_LOGI(TAG, "Starting background discovery and cleanup tasks");

    // Create continuous discovery task
    xTaskCreate(continuous_discovery_task, "esp_discovery", 3072, nullptr, 4, &discovery_task_handle);
    if (!discovery_task_handle) {
        ESP_LOGE(TAG, "Failed to create discovery task");
    }

    // Create peer cleanup task
    xTaskCreate(peer_cleanup_task, "peer_cleanup", 2048, nullptr, 3, &peer_cleanup_task_handle);
    if (!peer_cleanup_task_handle) {
        ESP_LOGE(TAG, "Failed to create peer cleanup task");
    }

    discovery_start_time_us = esp_timer_get_time();
    discovery_timing_active = true;
    float init_time_ms = (discovery_start_time_us - system_boot_time_us) / 1000.0f;

    ESP_LOGI(TAG, "ESP-NOW Performance Testing System initialized successfully!");
    ESP_LOGI(TAG, "Continuous discovery and peer management active");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "DISCOVERY_STARTED!");
    ESP_LOGI(TAG, "DISCOVERY_START_TIMESTAMP: %llu us", discovery_start_time_us);
    ESP_LOGI(TAG, "INITIALIZATION_TIME: %.3f ms", init_time_ms);
    ESP_LOGI(TAG, "STATUS: Actively searching for peers...");
    ESP_LOGI(TAG, "========================================");
}

void loop() {
    // Main application loop for ESP-NOW testing
    static uint32_t loop_count = 0;
    static bool tests_running = false;

    loop_count++;

    // Check if we have peers and start testing
    if (!tests_running && esp_now_manager->get_peer_count() > 0 && loop_count > 3) {
        ESP_LOGI(TAG, "Found %zu peers, starting performance tests", esp_now_manager->get_peer_count());

        // Display discovered peers
        auto peers = esp_now_manager->get_peers();
        for (size_t i = 0; i < peers.size(); i++) {
            ESP_LOGI(TAG, "Peer %zu: %02x:%02x:%02x:%02x:%02x:%02x",
                     i, peers[i].mac_addr[0], peers[i].mac_addr[1],
                     peers[i].mac_addr[2], peers[i].mac_addr[3],
                     peers[i].mac_addr[4], peers[i].mac_addr[5]);
        }

        tests_running = true;

        if (current_role == TEST_ROLE_COORDINATOR) {
            ESP_LOGI(TAG, "Running as COORDINATOR - Starting full test suite");
            performance_tests->run_full_performance_suite();
        } else {
            ESP_LOGI(TAG, "Running as PEER - Waiting for coordinator commands");
        }
    }

    // Periodic status updates
    if (loop_count % 10 == 0) {
        esp_now_statistics_t stats = esp_now_manager->get_statistics();
        ESP_LOGI(TAG, "ESP-NOW Statistics:");
        ESP_LOGI(TAG, "  Packets sent: %lu, received: %lu, lost: %lu",
                 stats.total_packets_sent, stats.total_packets_received, stats.total_packets_lost);
        ESP_LOGI(TAG, "  Bytes sent: %llu, received: %llu",
                 stats.total_bytes_sent, stats.total_bytes_received);
        ESP_LOGI(TAG, "  Discovery requests: %lu, responses: %lu",
                 stats.discovery_requests_sent, stats.discovery_responses_received);
        ESP_LOGI(TAG, "  Active peers: %zu", esp_now_manager->get_peer_count());
    }

    // Check for performance test commands via serial input (simulation)
    if (loop_count % 50 == 0 && tests_running) {
        ESP_LOGI(TAG, "Performance testing system active - %zu peers connected",
                 esp_now_manager->get_peer_count());

        // Demonstrate some basic tests periodically
        if (current_role == TEST_ROLE_PEER && !esp_now_manager->get_peers().empty()) {
            // Run a quick latency test as peer
            auto peers = esp_now_manager->get_peers();
            if (!peers.empty()) {
                ESP_LOGI(TAG, "Running quick ping test to first peer");
                esp_now_manager->send_ping(peers[0].mac_addr);
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second loop interval
}

// Optional: run loop() inside its own FreeRTOS task instead of blocking app_main
static void loop_task(void *arg) {
    (void)arg;
    for(;;) {
        loop();
    }
}

void start_loop_task(unsigned stackSize, UBaseType_t priority, BaseType_t core) {
    TaskHandle_t handle = nullptr;
    xTaskCreatePinnedToCore(loop_task, "loop_task", stackSize, nullptr, priority, &handle, core);
    if (!handle) {
        ESP_LOGE(TAG, "Failed to create loop task");
    }
}

// Only app_main needs C linkage so the ESP-IDF startup code can find it.
extern "C" void app_main(void) {
    setup();

    /* ========================================
     * CHOOSE ONE OF THE TWO APPROACHES BELOW:
     * ======================================== */

    // ---- OPTION A: Blocking loop (Arduino-style) ----
    // This blocks app_main() and runs loop() directly
    // Simpler but prevents app_main() from returning
    /*
    for(;;) {
        loop();
    }
    */

    // ---- OPTION B: Non-blocking task-based approach ----
    // This creates a separate FreeRTOS task for loop() and returns from app_main()
    // Better for complex applications with multiple high-priority tasks
    // Allows the idle task and other system tasks to run more efficiently

    ESP_LOGI(TAG, "Starting main loop in separate FreeRTOS task");
    start_loop_task(8192, 5, tskNO_AFFINITY); // 8KB stack, priority 5, any core

    // app_main() returns here, allowing ESP-IDF to continue with other system tasks
    // The loop() function continues running in its own task (loop_task)
}