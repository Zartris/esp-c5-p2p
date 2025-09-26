# ESP-NOW Network Performance Testing Plan
## ESP32-C5 P2P Communication Analysis

### Overview
This document outlines a comprehensive testing strategy for ESP-NOW networking performance on ESP32-C5 devices using 5GHz WiFi. The testing framework focuses on measuring device discovery speed, message latency, packet loss rates, and overall network reliability under various conditions.

## Test Objectives

### Primary Metrics
1. **Device Discovery Performance**
   - Time to discover first peer device
   - Time to discover N peer devices (scalability)
   - Discovery success rate at various distances
   - Discovery performance with interference

2. **Message Delivery Performance**
   - Round-trip latency (ping-pong tests)
   - One-way message delivery time
   - Throughput with variable payload sizes
   - Packet loss rates under different conditions

3. **Network Reliability**
   - Connection stability over time
   - Performance degradation with distance
   - Impact of environmental interference
   - Battery consumption during operations

4. **Scalability Analysis**
   - Performance vs. number of connected devices
   - Maximum effective range
   - Mesh network formation and maintenance

## Hardware Requirements

### Test Setup
- **Minimum 3 ESP32-C5 development boards** for comprehensive testing
- **Power supplies/batteries** for mobile testing scenarios
- **USB cables** for programming and monitoring
- **Measuring tools** (distance measuring device, RF analyzer if available)
- **Test environment** with controllable interference sources

### ESP32-C5 Configuration
- **5GHz WiFi Band**: Utilizing 802.11a/n for reduced interference
- **ESP-NOW Protocol**: Direct peer-to-peer communication
- **Flash Memory**: Sufficient for logging and data storage
- **GPIO Pins**: For status LEDs and external triggers

## Software Architecture

### Core Components

#### 1. ESP-NOW Manager (`esp_now_manager.hpp/.cpp`)
```cpp
class ESPNowManager {
public:
    // Initialization and configuration
    esp_err_t initialize(bool use_5ghz = true);
    esp_err_t add_peer(const uint8_t* mac_address);

    // Discovery functions
    esp_err_t start_discovery();
    esp_err_t broadcast_presence();

    // Communication functions
    esp_err_t send_message(const uint8_t* mac, const void* data, size_t len);
    esp_err_t send_broadcast(const void* data, size_t len);

    // Statistics and monitoring
    NetworkStats get_statistics();
    void reset_statistics();
};
```

#### 2. Test Framework (`test_framework.hpp/.cpp`)
```cpp
class TestFramework {
public:
    // Test execution
    void run_discovery_tests();
    void run_latency_tests();
    void run_throughput_tests();
    void run_reliability_tests();

    // Data collection
    void log_test_result(TestResult result);
    void export_results(const char* filename);

    // Test coordination
    void set_test_role(TestRole role); // COORDINATOR, PEER, OBSERVER
    void synchronize_test_start();
};
```

#### 3. Performance Tests (`performance_tests.hpp/.cpp`)
```cpp
class PerformanceTests {
public:
    // Discovery tests
    DiscoveryResult test_device_discovery(uint32_t timeout_ms);
    ScalabilityResult test_discovery_scalability(uint8_t max_devices);

    // Latency tests
    LatencyResult test_ping_pong(uint32_t packet_count);
    LatencyResult test_variable_payload(size_t min_size, size_t max_size);

    // Throughput tests
    ThroughputResult test_bulk_transfer(size_t total_bytes);
    ThroughputResult test_bidirectional_throughput();

    // Reliability tests
    ReliabilityResult test_packet_loss(uint32_t packet_count);
    ReliabilityResult test_distance_performance(uint8_t distance_steps);
};
```

## Test Scenarios

### 1. Device Discovery Tests

#### Test Case 1.1: Basic Discovery Speed
- **Objective**: Measure time to discover first peer device
- **Setup**: 2 devices, optimal distance (~1-2 meters)
- **Procedure**:
  1. Device A starts discovery broadcast
  2. Device B responds to discovery
  3. Measure time from broadcast to successful peer registration
- **Success Criteria**: < 500ms average discovery time

#### Test Case 1.2: Multi-Device Discovery
- **Objective**: Scalability of discovery with multiple devices
- **Setup**: 1 coordinator + N peer devices (N = 2, 4, 6, 8, 10)
- **Procedure**:
  1. Coordinator broadcasts discovery request
  2. All peers respond simultaneously
  3. Measure total discovery time and success rate
- **Success Criteria**: Linear scaling, >95% success rate up to 10 devices

#### Test Case 1.3: Range Discovery Testing
- **Objective**: Discovery performance vs. distance
- **Setup**: 2 devices at increasing distances (5m, 10m, 15m, 20m+)
- **Procedure**:
  1. Test discovery at each distance
  2. Record success rate and discovery time
  3. Identify maximum reliable range
- **Success Criteria**: Maintain >90% success rate up to specified range

### 2. Message Latency Tests

#### Test Case 2.1: Ping-Pong Latency
- **Objective**: Measure round-trip message latency
- **Setup**: 2 devices, various distances
- **Procedure**:
  1. Device A sends timestamped ping message
  2. Device B immediately responds with pong
  3. Device A calculates round-trip time
  4. Repeat 1000 times for statistical accuracy
- **Success Criteria**: < 10ms average latency at short range

#### Test Case 2.2: Variable Payload Latency
- **Objective**: Latency vs. message size
- **Setup**: 2 devices, payloads from 8 bytes to 250 bytes
- **Procedure**:
  1. Send messages of increasing payload size
  2. Measure latency for each size
  3. Identify optimal payload sizes
- **Success Criteria**: Predictable latency scaling

#### Test Case 2.3: One-Way Message Timing
- **Objective**: Accurate one-way delivery measurement
- **Setup**: 2 devices with synchronized clocks
- **Procedure**:
  1. Synchronize device clocks via NTP or manual sync
  2. Send timestamped messages
  3. Calculate one-way delivery time
- **Success Criteria**: < 5ms average one-way latency

### 3. Throughput and Reliability Tests

#### Test Case 3.1: Maximum Throughput
- **Objective**: Determine peak data transfer rates
- **Setup**: 2 devices, bulk data transfer
- **Procedure**:
  1. Send continuous stream of maximum-size packets
  2. Measure successful data rate
  3. Test both unidirectional and bidirectional
- **Success Criteria**: Achieve >80% of theoretical ESP-NOW bandwidth

#### Test Case 3.2: Packet Loss Analysis
- **Objective**: Measure packet loss under various conditions
- **Setup**: 2 devices, controlled interference
- **Procedure**:
  1. Send numbered sequence of packets
  2. Track missing packets at receiver
  3. Test with/without interference
- **Success Criteria**: < 1% packet loss in ideal conditions

#### Test Case 3.3: Environmental Stress Testing
- **Objective**: Performance under adverse conditions
- **Setup**: Multiple test environments
- **Procedure**:
  1. Test in various physical environments
  2. Introduce controlled interference (2.4GHz WiFi, Bluetooth)
  3. Test with moving devices
- **Success Criteria**: Graceful degradation, maintain connectivity

### 4. Long-term Stability Tests

#### Test Case 4.1: Extended Operation
- **Objective**: Long-term connection stability
- **Setup**: Multiple devices, 24+ hour test
- **Procedure**:
  1. Establish connections between all devices
  2. Maintain periodic communication
  3. Monitor for disconnections and reconnections
- **Success Criteria**: > 99% uptime over 24 hours

#### Test Case 4.2: Power Consumption Analysis
- **Objective**: Battery life and power efficiency
- **Setup**: Battery-powered devices with current measurement
- **Procedure**:
  1. Measure current consumption during various operations
  2. Calculate theoretical battery life
  3. Test power-saving modes
- **Success Criteria**: Document power consumption profiles

## 5GHz Configuration Requirements

### WiFi Band Configuration
```cpp
// Force 5GHz operation
wifi_config_t wifi_config = {};
wifi_config.sta.band = WIFI_BAND_5G;
wifi_config.sta.channel = 36; // 5GHz channel
```

### ESP-NOW 5GHz Setup
- **Primary Channel**: Channel 36 (5.180 GHz)
- **Backup Channels**: 40, 44, 48 (5.200-5.240 GHz)
- **Bandwidth**: 20MHz for compatibility
- **Power**: Maximum allowed transmission power

### Regulatory Considerations
- Ensure compliance with local 5GHz regulations
- Consider DFS (Dynamic Frequency Selection) requirements
- Implement proper power control

## Test Data Collection

### Metrics to Record
1. **Timing Data**
   - Discovery times (min, max, average, standard deviation)
   - Message latencies (round-trip and one-way)
   - Throughput measurements

2. **Reliability Data**
   - Packet loss rates
   - Connection success/failure rates
   - Reconnection times

3. **Environmental Data**
   - RSSI (Received Signal Strength Indicator)
   - Distance measurements
   - Interference levels
   - Temperature and humidity (if available)

### Data Export Formats
- **CSV**: For statistical analysis
- **JSON**: For structured data analysis
- **Real-time**: Serial output for live monitoring

## Test Execution Procedure

### Phase 1: Individual Component Testing
1. Verify ESP-NOW initialization on each device
2. Test basic message sending/receiving
3. Validate 5GHz operation
4. Confirm statistics collection

### Phase 2: Pair-wise Testing
1. Run all latency tests between device pairs
2. Execute throughput measurements
3. Perform range and reliability tests

### Phase 3: Multi-device Testing
1. Discovery scalability tests
2. Mesh network performance
3. Interference resilience testing

### Phase 4: Long-term Validation
1. Extended stability tests
2. Environmental stress testing
3. Power consumption analysis

## Expected Results and Success Criteria

### Discovery Performance Targets
- **Single device discovery**: < 500ms average
- **10-device discovery**: < 2 seconds total
- **Discovery range**: > 20 meters line-of-sight

### Latency Targets
- **Round-trip latency**: < 10ms average at short range
- **One-way latency**: < 5ms average
- **Latency stability**: < 2ms standard deviation

### Throughput Targets
- **Unidirectional**: > 200 kbps sustained
- **Bidirectional**: > 150 kbps sustained
- **Packet loss**: < 1% in ideal conditions

### Reliability Targets
- **Connection success**: > 99% in normal conditions
- **Long-term stability**: > 99% uptime over 24 hours
- **Interference resilience**: Graceful degradation, maintain basic connectivity

## Implementation Timeline

### Week 1: Core Infrastructure
- ESP-NOW manager implementation
- Basic test framework
- 5GHz configuration

### Week 2: Performance Tests
- Discovery tests implementation
- Latency measurement system
- Data logging infrastructure

### Week 3: Advanced Testing
- Throughput tests
- Reliability measurements
- Environmental stress tests

### Week 4: Validation and Documentation
- Multi-device testing
- Long-term stability tests
- Results analysis and reporting

## Risk Mitigation

### Potential Issues
1. **5GHz Range Limitations**: May have shorter range than 2.4GHz
2. **Regulatory Compliance**: Different regions have different 5GHz rules
3. **Hardware Variations**: Device-to-device performance differences
4. **Environmental Factors**: Interference and obstacles

### Mitigation Strategies
1. **Adaptive Power Control**: Adjust transmission power based on conditions
2. **Channel Selection**: Dynamic channel switching for optimal performance
3. **Error Recovery**: Robust error handling and automatic reconnection
4. **Comprehensive Testing**: Test in various real-world conditions

## Conclusion

This comprehensive testing plan provides a structured approach to evaluating ESP-NOW performance on ESP32-C5 devices using 5GHz WiFi. The testing framework will generate detailed performance metrics that can be used to optimize network parameters and validate the system's suitability for specific applications.

The modular test architecture allows for easy extension and modification of test cases, making it suitable for ongoing performance validation and optimization efforts.