#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include "esp_now_manager.hpp"

#define TEST_FRAMEWORK_TAG "TEST_FW"

typedef enum {
    TEST_ROLE_COORDINATOR = 0,
    TEST_ROLE_PEER = 1,
    TEST_ROLE_OBSERVER = 2
} test_role_t;

typedef enum {
    TEST_STATUS_PENDING = 0,
    TEST_STATUS_RUNNING = 1,
    TEST_STATUS_COMPLETED = 2,
    TEST_STATUS_FAILED = 3
} test_status_t;

typedef struct {
    std::string test_name;
    test_status_t status;
    uint64_t start_time_us;
    uint64_t end_time_us;
    uint32_t iterations_completed;
    uint32_t iterations_total;
    std::string error_message;

    // Metrics storage
    std::vector<float> latency_measurements;
    std::vector<uint32_t> throughput_measurements;
    std::vector<float> packet_loss_rates;
    std::vector<int8_t> rssi_measurements;

    // Summary statistics
    float avg_latency_ms;
    float min_latency_ms;
    float max_latency_ms;
    float stddev_latency_ms;
    uint32_t avg_throughput_bps;
    float avg_packet_loss_percent;
    int8_t avg_rssi_dbm;

    // Test specific data
    uint32_t discovery_time_ms;
    uint32_t devices_discovered;
    uint32_t max_range_meters;
    bool reliability_passed;
} test_result_t;

typedef struct {
    test_role_t role;
    uint8_t coordinator_mac[6];
    uint32_t test_duration_ms;
    uint32_t test_iterations;
    bool enable_logging;
    std::string log_filename;
} test_configuration_t;

typedef std::function<void(const test_result_t&)> test_completed_callback_t;
typedef std::function<void(const std::string&, uint32_t, uint32_t)> test_progress_callback_t;

class TestFramework {
private:
    bool initialized_;
    test_role_t role_;
    test_configuration_t config_;
    ESPNowManager& esp_now_manager_;

    std::vector<test_result_t> test_results_;
    SemaphoreHandle_t results_mutex_;
    TaskHandle_t coordination_task_handle_;

    test_completed_callback_t test_completed_callback_;
    test_progress_callback_t test_progress_callback_;

    uint64_t get_timestamp_us();
    void calculate_statistics(test_result_t& result);
    void log_test_result(const test_result_t& result);

    static void coordination_task(void *parameter);
    void handle_coordination_messages();

public:
    TestFramework();
    ~TestFramework();

    esp_err_t initialize(test_role_t role, const test_configuration_t& config);
    esp_err_t deinitialize();

    // Test execution control
    esp_err_t start_test_session();
    esp_err_t stop_test_session();
    esp_err_t synchronize_test_start(uint32_t timeout_ms = 10000);

    // Individual test execution
    esp_err_t run_discovery_test(const std::string& test_name, uint32_t timeout_ms);
    esp_err_t run_latency_test(const std::string& test_name, const uint8_t* target_mac,
                              uint32_t ping_count);
    esp_err_t run_throughput_test(const std::string& test_name, const uint8_t* target_mac,
                                 uint32_t duration_ms, size_t payload_size);
    esp_err_t run_reliability_test(const std::string& test_name, const uint8_t* target_mac,
                                  uint32_t packet_count, uint32_t interval_ms);
    esp_err_t run_range_test(const std::string& test_name, const uint8_t* target_mac,
                            uint32_t step_duration_ms);

    // Test suite execution
    esp_err_t run_all_discovery_tests();
    esp_err_t run_all_performance_tests();
    esp_err_t run_full_test_suite();

    // Results management
    std::vector<test_result_t> get_test_results();
    test_result_t* get_test_result(const std::string& test_name);
    void clear_test_results();

    // Data export
    esp_err_t export_results_csv(const std::string& filename);
    esp_err_t export_results_json(const std::string& filename);
    esp_err_t print_test_summary();

    // Configuration
    void set_test_configuration(const test_configuration_t& config);
    test_configuration_t get_test_configuration();

    // Callbacks
    void set_test_completed_callback(test_completed_callback_t callback);
    void set_test_progress_callback(test_progress_callback_t callback);

    // Static utilities
    static std::string format_mac_address(const uint8_t* mac);
    static std::string format_timestamp(uint64_t timestamp_us);
    static float calculate_packet_loss_rate(uint32_t sent, uint32_t received);
    static float calculate_average(const std::vector<float>& values);
    static float calculate_standard_deviation(const std::vector<float>& values, float mean);
};