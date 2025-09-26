#pragma once

#include "test_framework.hpp"
#include "esp_now_manager.hpp"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <functional>

#define PERFORMANCE_TESTS_TAG "PERF_TESTS"

typedef struct {
    uint32_t packet_size;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t duration_ms;
    float throughput_bps;
    float packet_loss_percent;
    float avg_latency_ms;
    int8_t avg_rssi_dbm;
} throughput_test_result_t;

typedef struct {
    uint32_t ping_count;
    std::vector<float> latency_measurements;
    float min_latency_ms;
    float max_latency_ms;
    float avg_latency_ms;
    float stddev_latency_ms;
    float jitter_ms;
    uint32_t packets_lost;
    float packet_loss_percent;
} latency_test_result_t;

typedef struct {
    uint32_t test_distance_meters;
    std::vector<int8_t> rssi_measurements;
    int8_t min_rssi_dbm;
    int8_t max_rssi_dbm;
    int8_t avg_rssi_dbm;
    uint32_t packets_sent;
    uint32_t packets_received;
    float packet_loss_percent;
    bool connection_stable;
} range_test_result_t;

typedef struct {
    uint32_t devices_found;
    uint32_t discovery_time_ms;
    std::vector<uint32_t> individual_discovery_times;
    float avg_discovery_time_ms;
    float min_discovery_time_ms;
    float max_discovery_time_ms;
    bool all_devices_discovered;
} discovery_test_result_t;

typedef struct {
    uint32_t test_duration_hours;
    uint32_t total_packets_sent;
    uint32_t total_packets_received;
    uint32_t connection_drops;
    uint32_t reconnection_attempts;
    uint32_t successful_reconnections;
    float avg_packet_loss_percent;
    float uptime_percent;
    std::vector<uint64_t> connection_drop_times;
    std::vector<uint32_t> reconnection_times_ms;
} stability_test_result_t;

class PerformanceTests {
private:
    TestFramework& test_framework_;
    ESPNowManager& esp_now_manager_;

    // Test state tracking
    bool test_active_;
    uint32_t current_test_sequence_;
    std::vector<uint64_t> ping_timestamps_;
    std::vector<uint32_t> ping_sequence_numbers_;

    // Callback tracking for ping responses
    std::function<void(uint32_t, float)> ping_response_callback_;

    // Test utilities
    void setup_ping_response_handler();
    float calculate_jitter(const std::vector<float>& latencies);
    int8_t simulate_rssi_measurement(); // In real implementation, would get actual RSSI
    void log_throughput_result(const throughput_test_result_t& result);
    void log_latency_result(const latency_test_result_t& result);
    void log_range_result(const range_test_result_t& result);
    void log_discovery_result(const discovery_test_result_t& result);
    void log_stability_result(const stability_test_result_t& result);

public:
    PerformanceTests(TestFramework& framework, ESPNowManager& manager);
    ~PerformanceTests();

    // Discovery Performance Tests
    esp_err_t test_basic_discovery(discovery_test_result_t& result, uint32_t timeout_ms = 10000);
    esp_err_t test_multi_device_discovery(discovery_test_result_t& result,
                                         uint32_t expected_devices, uint32_t timeout_ms = 15000);
    esp_err_t test_discovery_scalability(std::vector<discovery_test_result_t>& results,
                                        uint32_t max_devices = 10);

    // Latency Performance Tests
    esp_err_t test_ping_pong_latency(latency_test_result_t& result, const uint8_t* target_mac,
                                    uint32_t ping_count = 1000);
    esp_err_t test_variable_payload_latency(std::vector<latency_test_result_t>& results,
                                           const uint8_t* target_mac, uint32_t ping_count = 100);
    esp_err_t test_concurrent_latency(latency_test_result_t& result,
                                     const std::vector<uint8_t*>& target_macs,
                                     uint32_t ping_count = 100);

    // Throughput Performance Tests
    esp_err_t test_unidirectional_throughput(throughput_test_result_t& result,
                                            const uint8_t* target_mac, uint32_t duration_ms = 30000,
                                            uint32_t packet_size = 200);
    esp_err_t test_bidirectional_throughput(std::vector<throughput_test_result_t>& results,
                                           const uint8_t* target_mac, uint32_t duration_ms = 30000,
                                           uint32_t packet_size = 200);
    esp_err_t test_variable_payload_throughput(std::vector<throughput_test_result_t>& results,
                                              const uint8_t* target_mac, uint32_t duration_ms = 15000);

    // Range and Reliability Tests
    esp_err_t test_distance_performance(std::vector<range_test_result_t>& results,
                                       const uint8_t* target_mac, uint32_t max_distance_meters = 50,
                                       uint32_t step_meters = 5);
    esp_err_t test_packet_loss_analysis(throughput_test_result_t& result,
                                       const uint8_t* target_mac, uint32_t packet_count = 10000);
    esp_err_t test_interference_resilience(std::vector<throughput_test_result_t>& results,
                                          const uint8_t* target_mac);

    // Long-term Stability Tests
    esp_err_t test_connection_stability(stability_test_result_t& result,
                                       const uint8_t* target_mac, uint32_t duration_hours = 24);
    esp_err_t test_power_consumption_analysis(uint32_t duration_minutes = 60);

    // Comprehensive Test Suites
    esp_err_t run_discovery_test_suite(std::vector<discovery_test_result_t>& results);
    esp_err_t run_latency_test_suite(std::vector<latency_test_result_t>& results,
                                    const uint8_t* target_mac);
    esp_err_t run_throughput_test_suite(std::vector<throughput_test_result_t>& results,
                                       const uint8_t* target_mac);
    esp_err_t run_reliability_test_suite(std::vector<range_test_result_t>& range_results,
                                        std::vector<throughput_test_result_t>& reliability_results,
                                        const uint8_t* target_mac);
    esp_err_t run_full_performance_suite();

    // Environmental Testing
    esp_err_t test_temperature_performance(const uint8_t* target_mac);
    esp_err_t test_mobility_performance(const uint8_t* target_mac);
    esp_err_t test_multi_path_performance(const uint8_t* target_mac);

    // Result Analysis and Reporting
    void analyze_latency_distribution(const latency_test_result_t& result);
    void analyze_throughput_consistency(const std::vector<throughput_test_result_t>& results);
    void generate_performance_report();

    // Test Control
    void abort_current_test();
    bool is_test_active() const;

    // Configuration
    void set_ping_response_callback(std::function<void(uint32_t, float)> callback);
};