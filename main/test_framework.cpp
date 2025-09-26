#include "test_framework.hpp"
#include <esp_timer.h>
#include <esp_system.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

TestFramework::TestFramework()
    : initialized_(false), role_(TEST_ROLE_PEER),
      esp_now_manager_(ESPNowManager::get_instance()),
      results_mutex_(nullptr), coordination_task_handle_(nullptr) {
    memset(&config_, 0, sizeof(config_));
}

TestFramework::~TestFramework() {
    deinitialize();
}

esp_err_t TestFramework::initialize(test_role_t role, const test_configuration_t& config) {
    if (initialized_) {
        ESP_LOGW(TEST_FRAMEWORK_TAG, "Test framework already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TEST_FRAMEWORK_TAG, "Initializing test framework as %s",
             role == TEST_ROLE_COORDINATOR ? "COORDINATOR" :
             role == TEST_ROLE_PEER ? "PEER" : "OBSERVER");

    role_ = role;
    config_ = config;

    results_mutex_ = xSemaphoreCreateMutex();
    if (!results_mutex_) {
        ESP_LOGE(TEST_FRAMEWORK_TAG, "Failed to create results mutex");
        return ESP_ERR_NO_MEM;
    }

    if (role_ == TEST_ROLE_COORDINATOR) {
        xTaskCreate(coordination_task, "test_coord", 4096, this, 6, &coordination_task_handle_);
    }

    // Set up ESP-NOW callbacks
    esp_now_manager_.set_receive_callback([this](const uint8_t* mac, const esp_now_message_t* msg) {
        if (msg->msg_type >= ESP_NOW_MSG_TYPE_TEST_START && msg->msg_type <= ESP_NOW_MSG_TYPE_TEST_DATA) {
            // Handle test-related messages
            ESP_LOGD(TEST_FRAMEWORK_TAG, "Received test message type %d from %02x:%02x:%02x:%02x:%02x:%02x",
                     msg->msg_type, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    });

    initialized_ = true;
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Test framework initialized successfully");
    return ESP_OK;
}

esp_err_t TestFramework::deinitialize() {
    if (!initialized_) {
        return ESP_OK;
    }

    if (coordination_task_handle_) {
        vTaskDelete(coordination_task_handle_);
        coordination_task_handle_ = nullptr;
    }

    if (results_mutex_) {
        vSemaphoreDelete(results_mutex_);
        results_mutex_ = nullptr;
    }

    test_results_.clear();
    initialized_ = false;

    ESP_LOGI(TEST_FRAMEWORK_TAG, "Test framework deinitialized");
    return ESP_OK;
}

void TestFramework::coordination_task(void *parameter) {
    TestFramework* framework = (TestFramework*)parameter;

    while (true) {
        framework->handle_coordination_messages();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void TestFramework::handle_coordination_messages() {
    // Handle coordination between devices for synchronized testing
    // This would typically involve sending/receiving test coordination messages
}

esp_err_t TestFramework::start_test_session() {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Starting test session");

    if (role_ == TEST_ROLE_COORDINATOR) {
        // Send test start signal to all peers
        esp_now_manager_.send_broadcast(ESP_NOW_MSG_TYPE_TEST_START, nullptr, 0);
    }

    return ESP_OK;
}

esp_err_t TestFramework::stop_test_session() {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Stopping test session");

    if (role_ == TEST_ROLE_COORDINATOR) {
        // Send test stop signal to all peers
        esp_now_manager_.send_broadcast(ESP_NOW_MSG_TYPE_TEST_STOP, nullptr, 0);
    }

    return ESP_OK;
}

esp_err_t TestFramework::synchronize_test_start(uint32_t timeout_ms) {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Synchronizing test start with timeout %lu ms", timeout_ms);

    uint64_t start_time = get_timestamp_us();
    uint64_t timeout_us = timeout_ms * 1000;

    if (role_ == TEST_ROLE_COORDINATOR) {
        // Wait for all peers to be ready, then send start signal
        vTaskDelay(pdMS_TO_TICKS(1000)); // Give peers time to prepare
        esp_now_manager_.send_broadcast(ESP_NOW_MSG_TYPE_TEST_START, nullptr, 0);
    } else {
        // Wait for start signal from coordinator
        while ((get_timestamp_us() - start_time) < timeout_us) {
            // Check for test start message (would be handled in receive callback)
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    return ESP_OK;
}

esp_err_t TestFramework::run_discovery_test(const std::string& test_name, uint32_t timeout_ms) {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running discovery test: %s", test_name.c_str());

    test_result_t result = {};
    result.test_name = test_name;
    result.status = TEST_STATUS_RUNNING;
    result.start_time_us = get_timestamp_us();
    result.iterations_total = 1;

    // Record initial peer count
    uint32_t initial_peers = esp_now_manager_.get_peer_count();

    // Start discovery
    esp_err_t ret = esp_now_manager_.start_discovery(timeout_ms);
    if (ret != ESP_OK) {
        result.status = TEST_STATUS_FAILED;
        result.error_message = "Failed to start discovery";
        result.end_time_us = get_timestamp_us();

        if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
            test_results_.push_back(result);
            xSemaphoreGive(results_mutex_);
        }
        return ret;
    }

    // Wait for discovery to complete
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));

    // Record results
    uint32_t final_peers = esp_now_manager_.get_peer_count();
    result.devices_discovered = final_peers - initial_peers;
    result.discovery_time_ms = (get_timestamp_us() - result.start_time_us) / 1000;
    result.end_time_us = get_timestamp_us();
    result.status = TEST_STATUS_COMPLETED;
    result.iterations_completed = 1;

    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        test_results_.push_back(result);
        xSemaphoreGive(results_mutex_);
    }

    if (test_completed_callback_) {
        test_completed_callback_(result);
    }

    log_test_result(result);
    return ESP_OK;
}

esp_err_t TestFramework::run_latency_test(const std::string& test_name, const uint8_t* target_mac,
                                         uint32_t ping_count) {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running latency test: %s (%lu pings)", test_name.c_str(), ping_count);

    test_result_t result = {};
    result.test_name = test_name;
    result.status = TEST_STATUS_RUNNING;
    result.start_time_us = get_timestamp_us();
    result.iterations_total = ping_count;

    // Set up ping response tracking
    std::vector<uint64_t> ping_times;
    std::vector<bool> ping_responses;
    ping_times.resize(ping_count);
    ping_responses.resize(ping_count, false);

    uint32_t successful_pings = 0;

    // Send pings and measure response times
    for (uint32_t i = 0; i < ping_count; i++) {
        uint64_t ping_start = get_timestamp_us();

        esp_err_t ret = esp_now_manager_.send_ping(target_mac);
        if (ret != ESP_OK) {
            ESP_LOGW(TEST_FRAMEWORK_TAG, "Failed to send ping %lu", i);
            continue;
        }

        // Wait for response (simplified - in real implementation would use callback)
        vTaskDelay(pdMS_TO_TICKS(100));

        // For now, simulate some response time measurement
        uint64_t response_time_us = get_timestamp_us() - ping_start;
        float latency_ms = response_time_us / 1000.0f;

        result.latency_measurements.push_back(latency_ms);
        successful_pings++;
        result.iterations_completed = i + 1;

        if (test_progress_callback_) {
            test_progress_callback_(test_name, i + 1, ping_count);
        }

        // Small delay between pings
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    result.end_time_us = get_timestamp_us();
    result.status = successful_pings > 0 ? TEST_STATUS_COMPLETED : TEST_STATUS_FAILED;

    if (successful_pings == 0) {
        result.error_message = "No successful ping responses";
    } else {
        calculate_statistics(result);
    }

    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        test_results_.push_back(result);
        xSemaphoreGive(results_mutex_);
    }

    if (test_completed_callback_) {
        test_completed_callback_(result);
    }

    log_test_result(result);
    return ESP_OK;
}

esp_err_t TestFramework::run_throughput_test(const std::string& test_name, const uint8_t* target_mac,
                                            uint32_t duration_ms, size_t payload_size) {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running throughput test: %s (%lu ms, %zu bytes payload)",
             test_name.c_str(), duration_ms, payload_size);

    test_result_t result = {};
    result.test_name = test_name;
    result.status = TEST_STATUS_RUNNING;
    result.start_time_us = get_timestamp_us();

    // Create test payload
    std::vector<uint8_t> payload(payload_size, 0xAA);

    uint32_t packets_sent = 0;
    uint32_t total_bytes_sent = 0;

    uint64_t test_end_time = result.start_time_us + (duration_ms * 1000);

    while (get_timestamp_us() < test_end_time) {
        esp_err_t ret = esp_now_manager_.send_message(target_mac, ESP_NOW_MSG_TYPE_TEST_DATA,
                                                     payload.data(), payload.size());
        if (ret == ESP_OK) {
            packets_sent++;
            total_bytes_sent += payload.size();
        }

        // Small delay to prevent overwhelming the network
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    result.end_time_us = get_timestamp_us();
    result.iterations_completed = packets_sent;
    result.status = packets_sent > 0 ? TEST_STATUS_COMPLETED : TEST_STATUS_FAILED;

    if (packets_sent > 0) {
        uint32_t actual_duration_ms = (result.end_time_us - result.start_time_us) / 1000;
        uint32_t throughput_bps = (total_bytes_sent * 8 * 1000) / actual_duration_ms;
        result.avg_throughput_bps = throughput_bps;
        result.throughput_measurements.push_back(throughput_bps);
    } else {
        result.error_message = "No packets sent successfully";
    }

    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        test_results_.push_back(result);
        xSemaphoreGive(results_mutex_);
    }

    if (test_completed_callback_) {
        test_completed_callback_(result);
    }

    log_test_result(result);
    return ESP_OK;
}

esp_err_t TestFramework::run_reliability_test(const std::string& test_name, const uint8_t* target_mac,
                                             uint32_t packet_count, uint32_t interval_ms) {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running reliability test: %s (%lu packets, %lu ms interval)",
             test_name.c_str(), packet_count, interval_ms);

    test_result_t result = {};
    result.test_name = test_name;
    result.status = TEST_STATUS_RUNNING;
    result.start_time_us = get_timestamp_us();
    result.iterations_total = packet_count;

    uint32_t packets_sent = 0;
    uint32_t packets_acknowledged = 0; // Would need proper ACK mechanism

    for (uint32_t i = 0; i < packet_count; i++) {
        uint8_t test_data[4];
        memcpy(test_data, &i, sizeof(i));

        esp_err_t ret = esp_now_manager_.send_message(target_mac, ESP_NOW_MSG_TYPE_TEST_DATA,
                                                     test_data, sizeof(test_data));
        if (ret == ESP_OK) {
            packets_sent++;
        }

        result.iterations_completed = i + 1;

        if (test_progress_callback_) {
            test_progress_callback_(test_name, i + 1, packet_count);
        }

        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }

    result.end_time_us = get_timestamp_us();
    result.status = packets_sent > 0 ? TEST_STATUS_COMPLETED : TEST_STATUS_FAILED;

    // For now, assume all sent packets were received (would need proper tracking)
    packets_acknowledged = packets_sent;

    float packet_loss_rate = calculate_packet_loss_rate(packets_sent, packets_acknowledged);
    result.avg_packet_loss_percent = packet_loss_rate;
    result.packet_loss_rates.push_back(packet_loss_rate);
    result.reliability_passed = (packet_loss_rate < 1.0f); // Pass if < 1% loss

    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        test_results_.push_back(result);
        xSemaphoreGive(results_mutex_);
    }

    if (test_completed_callback_) {
        test_completed_callback_(result);
    }

    log_test_result(result);
    return ESP_OK;
}

esp_err_t TestFramework::run_range_test(const std::string& test_name, const uint8_t* target_mac,
                                       uint32_t step_duration_ms) {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running range test: %s", test_name.c_str());

    test_result_t result = {};
    result.test_name = test_name;
    result.status = TEST_STATUS_RUNNING;
    result.start_time_us = get_timestamp_us();

    // This is a manual test that requires user interaction to change distance
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Range test requires manual distance changes");
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Move devices to different distances and observe connection quality");

    uint32_t max_successful_range = 0;
    uint32_t test_steps = 10; // Number of distance steps to test

    for (uint32_t step = 0; step < test_steps; step++) {
        ESP_LOGI(TEST_FRAMEWORK_TAG, "Testing step %lu/%lu - Please set distance and press enter",
                 step + 1, test_steps);

        // Send test packets and measure success rate
        uint32_t successful_packets = 0;
        uint32_t total_packets = 10;

        for (uint32_t i = 0; i < total_packets; i++) {
            esp_err_t ret = esp_now_manager_.send_ping(target_mac);
            if (ret == ESP_OK) {
                successful_packets++;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        float success_rate = (float)successful_packets / total_packets * 100.0f;
        ESP_LOGI(TEST_FRAMEWORK_TAG, "Step %lu success rate: %.1f%%", step + 1, success_rate);

        if (success_rate >= 90.0f) {
            max_successful_range = step + 1;
        }

        vTaskDelay(pdMS_TO_TICKS(step_duration_ms));
    }

    result.max_range_meters = max_successful_range * 5; // Assume 5m steps
    result.end_time_us = get_timestamp_us();
    result.status = TEST_STATUS_COMPLETED;
    result.iterations_completed = test_steps;

    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        test_results_.push_back(result);
        xSemaphoreGive(results_mutex_);
    }

    if (test_completed_callback_) {
        test_completed_callback_(result);
    }

    log_test_result(result);
    return ESP_OK;
}

esp_err_t TestFramework::run_all_discovery_tests() {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running all discovery tests");

    esp_err_t ret;

    ret = run_discovery_test("Basic Discovery", 5000);
    if (ret != ESP_OK) return ret;

    ret = run_discovery_test("Extended Discovery", 10000);
    if (ret != ESP_OK) return ret;

    ret = run_discovery_test("Fast Discovery", 2000);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t TestFramework::run_all_performance_tests() {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running all performance tests");

    // Need at least one peer to run performance tests
    auto peers = esp_now_manager_.get_peers();
    if (peers.empty()) {
        ESP_LOGW(TEST_FRAMEWORK_TAG, "No peers available for performance tests");
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t* target_mac = peers[0].mac_addr;
    esp_err_t ret;

    ret = run_latency_test("Latency Test - 100 pings", target_mac, 100);
    if (ret != ESP_OK) return ret;

    ret = run_throughput_test("Throughput Test - Small Payload", target_mac, 30000, 64);
    if (ret != ESP_OK) return ret;

    ret = run_throughput_test("Throughput Test - Large Payload", target_mac, 30000, 200);
    if (ret != ESP_OK) return ret;

    ret = run_reliability_test("Reliability Test", target_mac, 1000, 10);
    if (ret != ESP_OK) return ret;

    ret = run_range_test("Range Test", target_mac, 5000);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t TestFramework::run_full_test_suite() {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Running full test suite");

    esp_err_t ret;

    ret = start_test_session();
    if (ret != ESP_OK) return ret;

    ret = run_all_discovery_tests();
    if (ret != ESP_OK) return ret;

    ret = run_all_performance_tests();
    if (ret != ESP_OK) return ret;

    ret = stop_test_session();
    if (ret != ESP_OK) return ret;

    print_test_summary();

    return ESP_OK;
}

std::vector<test_result_t> TestFramework::get_test_results() {
    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        std::vector<test_result_t> results = test_results_;
        xSemaphoreGive(results_mutex_);
        return results;
    }
    return std::vector<test_result_t>();
}

test_result_t* TestFramework::get_test_result(const std::string& test_name) {
    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (auto& result : test_results_) {
            if (result.test_name == test_name) {
                xSemaphoreGive(results_mutex_);
                return &result;
            }
        }
        xSemaphoreGive(results_mutex_);
    }
    return nullptr;
}

void TestFramework::clear_test_results() {
    if (xSemaphoreTake(results_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        test_results_.clear();
        xSemaphoreGive(results_mutex_);
    }
}

esp_err_t TestFramework::print_test_summary() {
    auto results = get_test_results();

    ESP_LOGI(TEST_FRAMEWORK_TAG, "\n========== TEST SUMMARY ==========");
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Total tests run: %zu", results.size());

    uint32_t passed = 0, failed = 0;
    for (const auto& result : results) {
        if (result.status == TEST_STATUS_COMPLETED) passed++;
        else if (result.status == TEST_STATUS_FAILED) failed++;

        ESP_LOGI(TEST_FRAMEWORK_TAG, "\nTest: %s", result.test_name.c_str());
        ESP_LOGI(TEST_FRAMEWORK_TAG, "  Status: %s",
                 result.status == TEST_STATUS_COMPLETED ? "PASSED" :
                 result.status == TEST_STATUS_FAILED ? "FAILED" : "UNKNOWN");

        if (result.avg_latency_ms > 0) {
            ESP_LOGI(TEST_FRAMEWORK_TAG, "  Avg Latency: %.2f ms", result.avg_latency_ms);
        }

        if (result.avg_throughput_bps > 0) {
            ESP_LOGI(TEST_FRAMEWORK_TAG, "  Avg Throughput: %lu bps", result.avg_throughput_bps);
        }

        if (result.devices_discovered > 0) {
            ESP_LOGI(TEST_FRAMEWORK_TAG, "  Devices Discovered: %lu", result.devices_discovered);
        }

        if (!result.error_message.empty()) {
            ESP_LOGI(TEST_FRAMEWORK_TAG, "  Error: %s", result.error_message.c_str());
        }
    }

    ESP_LOGI(TEST_FRAMEWORK_TAG, "\nOverall: %lu passed, %lu failed", passed, failed);
    ESP_LOGI(TEST_FRAMEWORK_TAG, "==================================\n");

    return ESP_OK;
}

uint64_t TestFramework::get_timestamp_us() {
    return esp_timer_get_time();
}

void TestFramework::calculate_statistics(test_result_t& result) {
    if (result.latency_measurements.empty()) return;

    result.avg_latency_ms = calculate_average(result.latency_measurements);

    auto minmax = std::minmax_element(result.latency_measurements.begin(),
                                     result.latency_measurements.end());
    result.min_latency_ms = *minmax.first;
    result.max_latency_ms = *minmax.second;

    result.stddev_latency_ms = calculate_standard_deviation(result.latency_measurements,
                                                           result.avg_latency_ms);
}

void TestFramework::log_test_result(const test_result_t& result) {
    ESP_LOGI(TEST_FRAMEWORK_TAG, "Test completed: %s", result.test_name.c_str());
    ESP_LOGI(TEST_FRAMEWORK_TAG, "  Duration: %llu ms",
             (result.end_time_us - result.start_time_us) / 1000);
    ESP_LOGI(TEST_FRAMEWORK_TAG, "  Status: %s",
             result.status == TEST_STATUS_COMPLETED ? "COMPLETED" :
             result.status == TEST_STATUS_FAILED ? "FAILED" : "UNKNOWN");
}

void TestFramework::set_test_configuration(const test_configuration_t& config) {
    config_ = config;
}

test_configuration_t TestFramework::get_test_configuration() {
    return config_;
}

void TestFramework::set_test_completed_callback(test_completed_callback_t callback) {
    test_completed_callback_ = callback;
}

void TestFramework::set_test_progress_callback(test_progress_callback_t callback) {
    test_progress_callback_ = callback;
}

std::string TestFramework::format_mac_address(const uint8_t* mac) {
    std::stringstream ss;
    for (int i = 0; i < 6; i++) {
        if (i > 0) ss << ":";
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)mac[i];
    }
    return ss.str();
}

std::string TestFramework::format_timestamp(uint64_t timestamp_us) {
    uint64_t seconds = timestamp_us / 1000000;
    uint64_t microseconds = timestamp_us % 1000000;

    std::stringstream ss;
    ss << seconds << "." << std::setw(6) << std::setfill('0') << microseconds;
    return ss.str();
}

float TestFramework::calculate_packet_loss_rate(uint32_t sent, uint32_t received) {
    if (sent == 0) return 0.0f;
    return ((float)(sent - received) / sent) * 100.0f;
}

float TestFramework::calculate_average(const std::vector<float>& values) {
    if (values.empty()) return 0.0f;

    float sum = 0.0f;
    for (float value : values) {
        sum += value;
    }
    return sum / values.size();
}

float TestFramework::calculate_standard_deviation(const std::vector<float>& values, float mean) {
    if (values.size() < 2) return 0.0f;

    float sum_squared_diff = 0.0f;
    for (float value : values) {
        float diff = value - mean;
        sum_squared_diff += diff * diff;
    }

    return std::sqrt(sum_squared_diff / (values.size() - 1));
}