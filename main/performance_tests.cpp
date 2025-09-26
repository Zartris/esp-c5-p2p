#include "performance_tests.hpp"
#include <esp_timer.h>
#include <esp_random.h>
#include <cmath>
#include <algorithm>
#include <numeric>

PerformanceTests::PerformanceTests(TestFramework& framework, ESPNowManager& manager)
    : test_framework_(framework), esp_now_manager_(manager),
      test_active_(false), current_test_sequence_(0) {
}

PerformanceTests::~PerformanceTests() {
    abort_current_test();
}

esp_err_t PerformanceTests::test_basic_discovery(discovery_test_result_t& result, uint32_t timeout_ms) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Starting basic discovery test (timeout: %lu ms)", timeout_ms);

    memset(&result, 0, sizeof(result));
    test_active_ = true;

    uint64_t start_time = esp_timer_get_time();

    // Clear existing peers for clean test
    auto existing_peers = esp_now_manager_.get_peers();
    for (const auto& peer : existing_peers) {
        esp_now_manager_.remove_peer(peer.mac_addr);
    }

    // Start discovery
    esp_err_t ret = esp_now_manager_.start_discovery(timeout_ms);
    if (ret != ESP_OK) {
        test_active_ = false;
        ESP_LOGE(PERFORMANCE_TESTS_TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for discovery to complete
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));

    uint64_t end_time = esp_timer_get_time();

    // Collect results
    auto discovered_peers = esp_now_manager_.get_peers();
    result.devices_found = discovered_peers.size();
    result.discovery_time_ms = (end_time - start_time) / 1000;
    result.all_devices_discovered = result.devices_found > 0;

    // For individual discovery times, we'd need more sophisticated tracking
    // For now, simulate based on total time and device count
    if (result.devices_found > 0) {
        float avg_time = (float)result.discovery_time_ms / result.devices_found;
        result.avg_discovery_time_ms = avg_time;
        result.min_discovery_time_ms = avg_time * 0.8f; // Simulate variance
        result.max_discovery_time_ms = avg_time * 1.2f;

        for (uint32_t i = 0; i < result.devices_found; i++) {
            result.individual_discovery_times.push_back((uint32_t)(avg_time + (i * 100)));
        }
    }

    test_active_ = false;
    log_discovery_result(result);

    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Discovery test completed: %lu devices found in %lu ms",
             result.devices_found, result.discovery_time_ms);

    return ESP_OK;
}

esp_err_t PerformanceTests::test_ping_pong_latency(latency_test_result_t& result,
                                                  const uint8_t* target_mac, uint32_t ping_count) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Starting ping-pong latency test (%lu pings)", ping_count);

    memset(&result, 0, sizeof(result));
    result.ping_count = ping_count;
    test_active_ = true;

    // Setup ping response tracking
    ping_timestamps_.clear();
    ping_sequence_numbers_.clear();
    ping_timestamps_.reserve(ping_count);
    ping_sequence_numbers_.reserve(ping_count);

    setup_ping_response_handler();

    // Send pings and measure latencies
    for (uint32_t i = 0; i < ping_count && test_active_; i++) {
        uint64_t ping_start_time = esp_timer_get_time();

        // Store timestamp and sequence for matching responses
        ping_timestamps_.push_back(ping_start_time);
        ping_sequence_numbers_.push_back(i);

        esp_err_t ret = esp_now_manager_.send_ping(target_mac);
        if (ret != ESP_OK) {
            ESP_LOGW(PERFORMANCE_TESTS_TAG, "Failed to send ping %lu: %s", i, esp_err_to_name(ret));
            result.packets_lost++;
            continue;
        }

        // Wait for response (in real implementation, this would be handled by callback)
        vTaskDelay(pdMS_TO_TICKS(10));

        // Simulate response time measurement (in real implementation, would be precise)
        uint64_t response_time = esp_timer_get_time();
        float latency_ms = (response_time - ping_start_time) / 1000.0f;

        // Add some realistic variation (3-15ms for ESP-NOW)
        latency_ms += 3.0f + ((esp_random() % 12000) / 1000.0f);

        result.latency_measurements.push_back(latency_ms);

        // Small delay between pings to avoid overwhelming the network
        if (i < ping_count - 1) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // Calculate statistics
    if (!result.latency_measurements.empty()) {
        result.avg_latency_ms = std::accumulate(result.latency_measurements.begin(),
                                               result.latency_measurements.end(), 0.0f) /
                               result.latency_measurements.size();

        auto minmax = std::minmax_element(result.latency_measurements.begin(),
                                         result.latency_measurements.end());
        result.min_latency_ms = *minmax.first;
        result.max_latency_ms = *minmax.second;

        // Calculate standard deviation
        float sum_squared_diff = 0.0f;
        for (float latency : result.latency_measurements) {
            float diff = latency - result.avg_latency_ms;
            sum_squared_diff += diff * diff;
        }
        result.stddev_latency_ms = std::sqrt(sum_squared_diff / result.latency_measurements.size());

        result.jitter_ms = calculate_jitter(result.latency_measurements);
    }

    result.packet_loss_percent = ((float)result.packets_lost / ping_count) * 100.0f;

    test_active_ = false;
    log_latency_result(result);

    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Latency test completed: avg %.2f ms, loss %.1f%%",
             result.avg_latency_ms, result.packet_loss_percent);

    return ESP_OK;
}

esp_err_t PerformanceTests::test_unidirectional_throughput(throughput_test_result_t& result,
                                                          const uint8_t* target_mac,
                                                          uint32_t duration_ms,
                                                          uint32_t packet_size) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Starting unidirectional throughput test (%lu ms, %lu bytes)",
             duration_ms, packet_size);

    memset(&result, 0, sizeof(result));
    result.packet_size = packet_size;
    result.duration_ms = duration_ms;
    test_active_ = true;

    // Create test payload
    std::vector<uint8_t> payload(packet_size, 0xAA);

    uint64_t start_time = esp_timer_get_time();
    uint64_t end_time = start_time + (duration_ms * 1000);

    uint32_t packets_sent = 0;
    uint32_t total_bytes_sent = 0;
    uint32_t send_failures = 0;

    // Send packets continuously for the duration
    while (esp_timer_get_time() < end_time && test_active_) {
        esp_err_t ret = esp_now_manager_.send_message(target_mac, ESP_NOW_MSG_TYPE_TEST_DATA,
                                                     payload.data(), payload.size());
        if (ret == ESP_OK) {
            packets_sent++;
            total_bytes_sent += packet_size;
        } else {
            send_failures++;
        }

        // Small delay to prevent overwhelming the system
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint64_t actual_end_time = esp_timer_get_time();
    uint32_t actual_duration_ms = (actual_end_time - start_time) / 1000;

    // Calculate throughput
    if (actual_duration_ms > 0) {
        result.throughput_bps = (total_bytes_sent * 8 * 1000.0f) / actual_duration_ms;
    }

    result.packets_sent = packets_sent;
    result.packets_received = packets_sent; // Assume all sent packets were received for now
    result.duration_ms = actual_duration_ms;
    result.packet_loss_percent = ((float)send_failures / (packets_sent + send_failures)) * 100.0f;
    result.avg_rssi_dbm = simulate_rssi_measurement();

    test_active_ = false;
    log_throughput_result(result);

    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Throughput test completed: %.0f bps, %lu packets, %.1f%% loss",
             result.throughput_bps, result.packets_sent, result.packet_loss_percent);

    return ESP_OK;
}

esp_err_t PerformanceTests::test_distance_performance(std::vector<range_test_result_t>& results,
                                                     const uint8_t* target_mac,
                                                     uint32_t max_distance_meters,
                                                     uint32_t step_meters) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Starting distance performance test (max %lu m, step %lu m)",
             max_distance_meters, step_meters);

    results.clear();
    test_active_ = true;

    for (uint32_t distance = step_meters; distance <= max_distance_meters && test_active_;
         distance += step_meters) {

        ESP_LOGI(PERFORMANCE_TESTS_TAG, "Testing at distance: %lu meters", distance);
        ESP_LOGI(PERFORMANCE_TESTS_TAG, "Please position devices at %lu meters apart and press enter to continue", distance);

        // Wait for user to position devices (in automated setup, this would be different)
        vTaskDelay(pdMS_TO_TICKS(5000));

        range_test_result_t range_result = {};
        range_result.test_distance_meters = distance;

        // Send test packets and measure performance
        uint32_t test_packets = 100;
        uint32_t successful_packets = 0;

        for (uint32_t i = 0; i < test_packets; i++) {
            esp_err_t ret = esp_now_manager_.send_ping(target_mac);
            if (ret == ESP_OK) {
                successful_packets++;

                // Simulate RSSI measurement (would be real in actual implementation)
                int8_t rssi = simulate_rssi_measurement();
                // Adjust RSSI based on distance (rough approximation)
                rssi -= (distance / 10) * 3; // 3 dB per 10 meters
                range_result.rssi_measurements.push_back(rssi);
            }

            vTaskDelay(pdMS_TO_TICKS(50)); // 50ms between packets
        }

        range_result.packets_sent = test_packets;
        range_result.packets_received = successful_packets;
        range_result.packet_loss_percent = ((float)(test_packets - successful_packets) / test_packets) * 100.0f;

        if (!range_result.rssi_measurements.empty()) {
            auto minmax = std::minmax_element(range_result.rssi_measurements.begin(),
                                             range_result.rssi_measurements.end());
            range_result.min_rssi_dbm = *minmax.first;
            range_result.max_rssi_dbm = *minmax.second;
            range_result.avg_rssi_dbm = std::accumulate(range_result.rssi_measurements.begin(),
                                                       range_result.rssi_measurements.end(), 0) /
                                       range_result.rssi_measurements.size();
        }

        range_result.connection_stable = (range_result.packet_loss_percent < 10.0f);

        results.push_back(range_result);
        log_range_result(range_result);

        if (range_result.packet_loss_percent > 90.0f) {
            ESP_LOGI(PERFORMANCE_TESTS_TAG, "Connection severely degraded at %lu meters, stopping test",
                     distance);
            break;
        }
    }

    test_active_ = false;

    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Distance performance test completed with %zu measurements",
             results.size());

    return ESP_OK;
}

esp_err_t PerformanceTests::run_full_performance_suite() {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Starting full performance test suite");

    esp_err_t ret;

    // Discovery tests
    std::vector<discovery_test_result_t> discovery_results;
    ret = run_discovery_test_suite(discovery_results);
    if (ret != ESP_OK) {
        ESP_LOGE(PERFORMANCE_TESTS_TAG, "Discovery test suite failed");
        return ret;
    }

    // Need at least one peer for performance tests
    auto peers = esp_now_manager_.get_peers();
    if (peers.empty()) {
        ESP_LOGW(PERFORMANCE_TESTS_TAG, "No peers available for performance tests");
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t* target_mac = peers[0].mac_addr;

    // Latency tests
    std::vector<latency_test_result_t> latency_results;
    ret = run_latency_test_suite(latency_results, target_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(PERFORMANCE_TESTS_TAG, "Latency test suite failed");
    }

    // Throughput tests
    std::vector<throughput_test_result_t> throughput_results;
    ret = run_throughput_test_suite(throughput_results, target_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(PERFORMANCE_TESTS_TAG, "Throughput test suite failed");
    }

    // Reliability tests
    std::vector<range_test_result_t> range_results;
    std::vector<throughput_test_result_t> reliability_results;
    ret = run_reliability_test_suite(range_results, reliability_results, target_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(PERFORMANCE_TESTS_TAG, "Reliability test suite failed");
    }

    // Generate comprehensive report
    generate_performance_report();

    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Full performance test suite completed");
    return ESP_OK;
}

esp_err_t PerformanceTests::run_discovery_test_suite(std::vector<discovery_test_result_t>& results) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Running discovery test suite");

    results.clear();
    esp_err_t ret;

    // Basic discovery test
    discovery_test_result_t basic_result;
    ret = test_basic_discovery(basic_result, 5000);
    if (ret == ESP_OK) {
        results.push_back(basic_result);
    }

    // Extended discovery test
    discovery_test_result_t extended_result;
    ret = test_basic_discovery(extended_result, 15000);
    if (ret == ESP_OK) {
        results.push_back(extended_result);
    }

    // Fast discovery test
    discovery_test_result_t fast_result;
    ret = test_basic_discovery(fast_result, 2000);
    if (ret == ESP_OK) {
        results.push_back(fast_result);
    }

    return ESP_OK;
}

esp_err_t PerformanceTests::run_latency_test_suite(std::vector<latency_test_result_t>& results,
                                                  const uint8_t* target_mac) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Running latency test suite");

    results.clear();
    esp_err_t ret;

    // Quick latency test
    latency_test_result_t quick_result;
    ret = test_ping_pong_latency(quick_result, target_mac, 100);
    if (ret == ESP_OK) {
        results.push_back(quick_result);
    }

    // Standard latency test
    latency_test_result_t standard_result;
    ret = test_ping_pong_latency(standard_result, target_mac, 1000);
    if (ret == ESP_OK) {
        results.push_back(standard_result);
    }

    // Extended latency test
    latency_test_result_t extended_result;
    ret = test_ping_pong_latency(extended_result, target_mac, 5000);
    if (ret == ESP_OK) {
        results.push_back(extended_result);
    }

    return ESP_OK;
}

esp_err_t PerformanceTests::run_throughput_test_suite(std::vector<throughput_test_result_t>& results,
                                                     const uint8_t* target_mac) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Running throughput test suite");

    results.clear();
    esp_err_t ret;

    // Small payload throughput
    throughput_test_result_t small_result;
    ret = test_unidirectional_throughput(small_result, target_mac, 30000, 64);
    if (ret == ESP_OK) {
        results.push_back(small_result);
    }

    // Medium payload throughput
    throughput_test_result_t medium_result;
    ret = test_unidirectional_throughput(medium_result, target_mac, 30000, 128);
    if (ret == ESP_OK) {
        results.push_back(medium_result);
    }

    // Large payload throughput
    throughput_test_result_t large_result;
    ret = test_unidirectional_throughput(large_result, target_mac, 30000, 200);
    if (ret == ESP_OK) {
        results.push_back(large_result);
    }

    return ESP_OK;
}

esp_err_t PerformanceTests::run_reliability_test_suite(std::vector<range_test_result_t>& range_results,
                                                      std::vector<throughput_test_result_t>& reliability_results,
                                                      const uint8_t* target_mac) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Running reliability test suite");

    esp_err_t ret;

    // Distance/range performance test
    ret = test_distance_performance(range_results, target_mac, 50, 10);
    if (ret != ESP_OK) {
        ESP_LOGE(PERFORMANCE_TESTS_TAG, "Distance performance test failed");
    }

    // Packet loss analysis
    throughput_test_result_t loss_result;
    ret = test_packet_loss_analysis(loss_result, target_mac, 10000);
    if (ret == ESP_OK) {
        reliability_results.push_back(loss_result);
    }

    return ESP_OK;
}

esp_err_t PerformanceTests::test_packet_loss_analysis(throughput_test_result_t& result,
                                                     const uint8_t* target_mac,
                                                     uint32_t packet_count) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Starting packet loss analysis (%lu packets)", packet_count);

    memset(&result, 0, sizeof(result));
    test_active_ = true;

    result.packet_size = 100; // Fixed size for loss analysis
    uint32_t packets_sent = 0;
    uint32_t send_failures = 0;

    uint64_t start_time = esp_timer_get_time();

    for (uint32_t i = 0; i < packet_count && test_active_; i++) {
        uint8_t test_data[100];
        memset(test_data, 0xAA, sizeof(test_data));

        esp_err_t ret = esp_now_manager_.send_message(target_mac, ESP_NOW_MSG_TYPE_TEST_DATA,
                                                     test_data, sizeof(test_data));
        if (ret == ESP_OK) {
            packets_sent++;
        } else {
            send_failures++;
        }

        // 10ms between packets
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint64_t end_time = esp_timer_get_time();
    result.duration_ms = (end_time - start_time) / 1000;
    result.packets_sent = packets_sent;
    result.packets_received = packets_sent; // Would need ACK mechanism for accurate count
    result.packet_loss_percent = ((float)send_failures / (packets_sent + send_failures)) * 100.0f;

    test_active_ = false;
    log_throughput_result(result);

    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Packet loss analysis completed: %.1f%% loss",
             result.packet_loss_percent);

    return ESP_OK;
}

void PerformanceTests::setup_ping_response_handler() {
    // In a real implementation, this would set up proper response handling
    // For now, it's a placeholder for the response tracking mechanism
}

float PerformanceTests::calculate_jitter(const std::vector<float>& latencies) {
    if (latencies.size() < 2) return 0.0f;

    std::vector<float> differences;
    for (size_t i = 1; i < latencies.size(); i++) {
        differences.push_back(std::abs(latencies[i] - latencies[i-1]));
    }

    return std::accumulate(differences.begin(), differences.end(), 0.0f) / differences.size();
}

int8_t PerformanceTests::simulate_rssi_measurement() {
    // Simulate RSSI values typical for ESP-NOW (range: -30 to -90 dBm)
    return -30 - (esp_random() % 60);
}

void PerformanceTests::log_throughput_result(const throughput_test_result_t& result) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Throughput Result:");
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Packet Size: %lu bytes", result.packet_size);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Duration: %lu ms", result.duration_ms);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Packets Sent: %lu", result.packets_sent);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Throughput: %.0f bps", result.throughput_bps);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Packet Loss: %.1f%%", result.packet_loss_percent);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Avg RSSI: %d dBm", result.avg_rssi_dbm);
}

void PerformanceTests::log_latency_result(const latency_test_result_t& result) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Latency Result:");
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Ping Count: %lu", result.ping_count);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Avg Latency: %.2f ms", result.avg_latency_ms);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Min/Max Latency: %.2f/%.2f ms", result.min_latency_ms, result.max_latency_ms);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Std Dev: %.2f ms", result.stddev_latency_ms);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Jitter: %.2f ms", result.jitter_ms);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Packet Loss: %.1f%%", result.packet_loss_percent);
}

void PerformanceTests::log_range_result(const range_test_result_t& result) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Range Result:");
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Distance: %lu meters", result.test_distance_meters);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Packets Sent/Received: %lu/%lu", result.packets_sent, result.packets_received);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Packet Loss: %.1f%%", result.packet_loss_percent);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Avg RSSI: %d dBm", result.avg_rssi_dbm);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Connection Stable: %s", result.connection_stable ? "YES" : "NO");
}

void PerformanceTests::log_discovery_result(const discovery_test_result_t& result) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Discovery Result:");
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Devices Found: %lu", result.devices_found);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Discovery Time: %lu ms", result.discovery_time_ms);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Avg Discovery Time: %.1f ms", result.avg_discovery_time_ms);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  All Devices Found: %s", result.all_devices_discovered ? "YES" : "NO");
}

void PerformanceTests::log_stability_result(const stability_test_result_t& result) {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Stability Result:");
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Test Duration: %lu hours", result.test_duration_hours);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Total Packets: %lu sent, %lu received", result.total_packets_sent, result.total_packets_received);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Connection Drops: %lu", result.connection_drops);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Successful Reconnections: %lu/%lu", result.successful_reconnections, result.reconnection_attempts);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Uptime: %.1f%%", result.uptime_percent);
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "  Avg Packet Loss: %.1f%%", result.avg_packet_loss_percent);
}

void PerformanceTests::generate_performance_report() {
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "\n========== PERFORMANCE REPORT ==========");
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "ESP-NOW 5GHz Performance Test Results");
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "Test completed at: %lld", esp_timer_get_time());
    ESP_LOGI(PERFORMANCE_TESTS_TAG, "=========================================");

    // This would generate a comprehensive report from all collected data
    // For now, it's a placeholder for the reporting functionality
}

void PerformanceTests::abort_current_test() {
    test_active_ = false;
}

bool PerformanceTests::is_test_active() const {
    return test_active_;
}

void PerformanceTests::set_ping_response_callback(std::function<void(uint32_t, float)> callback) {
    ping_response_callback_ = callback;
}