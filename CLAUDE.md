# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

This is an ESP32-C5 ESP-NOW networking performance testing framework focused on **5GHz operation**. The goal is to characterize raw ESP-NOW capabilities (latency, reliability, device limits) for future mobile robot coordination applications. The project focuses on **network performance validation**, not robot implementation.

**Key Focus**: Testing ESP-NOW on 5GHz (Channel 36) to avoid 2.4GHz interference common in warehouse environments.

## Build and Development Commands

### ESP-IDF Commands
```bash
# Build the project
idf.py build

# Flash to ESP32-C5
idf.py flash

# Monitor serial output
idf.py monitor

# Build, flash, and monitor in one command
idf.py build flash monitor

# Clean build
idf.py clean

# Configure ESP-IDF settings
idf.py menuconfig
```

### Multi-Device Testing Commands
```bash
# Find connected devices
ls /dev/tty* | grep -E "(ACM|USB)"

# Flash specific devices
idf.py -p /dev/ttyACM0 flash  # Device A
idf.py -p /dev/ttyACM1 flash  # Device B

# Monitor specific devices (in separate terminals)
idf.py -p /dev/ttyACM0 monitor  # Terminal 1
idf.py -p /dev/ttyACM1 monitor  # Terminal 2
```

### VS Code Tasks (Alternative)
- **Build**: Ctrl+Shift+P → Tasks: Run Task → Build
- **Flash**: Ctrl+Shift+P → Tasks: Run Task → Flash
- **Monitor**: Ctrl+Shift+P → Tasks: Run Task → Monitor
- **Build, Flash and Monitor**: Ctrl+Shift+P → Tasks: Run Task → Build, Flash and Monitor

## Architecture Overview

### Core Components

**ESP-NOW Manager** (`main/esp_now_manager.hpp/.cpp`)
- Handles ESP-NOW initialization and 5GHz configuration (Channel 36)
- Manages peer discovery, connection, and message passing
- Provides statistics tracking and callback systems
- Implements continuous discovery task and peer cleanup

**Test Framework** (`main/test_framework.hpp/.cpp`)
- Orchestrates performance tests and data collection
- Supports coordinator/peer/observer test roles
- Handles test synchronization across multiple devices
- Collects and analyzes performance metrics

**Performance Tests** (`main/performance_tests.hpp/.cpp`)
- Implements specific test scenarios (latency, throughput, discovery, range)
- Measures key performance indicators for ESP-NOW
- Generates detailed performance reports

**Main Application** (`main/main.cpp`)
- Integrates all components and manages application lifecycle
- Runs continuous discovery and peer cleanup background tasks
- Provides main test loop with periodic statistics reporting

### Key Design Patterns

**Background Task Architecture**: The system uses dedicated FreeRTOS tasks for:
- Continuous peer discovery (1-second intervals with 3-packet bursts)
- Peer cleanup (removes stale peers after 60 seconds)
- ESP-NOW message handling (send/receive queues)

**Test Role System**: Devices can operate as:
- `TEST_ROLE_COORDINATOR`: Orchestrates test execution
- `TEST_ROLE_PEER`: Participates in tests
- `TEST_ROLE_OBSERVER`: Monitors test execution

**5GHz Configuration**: Optimized for 5GHz operation to avoid 2.4GHz interference in warehouse environments.

## Configuration Details

### 5GHz ESP-NOW Setup
- **Channel**: 36 (5.180 GHz)
- **Bandwidth**: 20MHz
- **Target**: ESP32-C5 with 5GHz capability
- **Max Peers**: Currently set to 20 for testing (removable limit)

### Key Configuration Files
- `sdkconfig.defaults`: ESP32-C5 and performance optimizations
- `main/CMakeLists.txt`: Component dependencies (esp_now, esp_wifi, etc.)
- `CONTEXT.md`: Detailed requirements and use case context

## Testing Philosophy

The framework tests **raw ESP-NOW networking capabilities** rather than application logic:

### Core Performance Metrics
1. **Discovery Speed**: Time to find and connect to peers
2. **Message Latency**: Round-trip and one-way message timing
3. **Throughput**: Data transfer rates with various payload sizes
4. **Packet Loss**: Reliability under different conditions
5. **Device Limits**: Maximum practical peer connections
6. **Range**: Effective communication distance

### Test Execution Flow
1. System initializes ESP-NOW manager and test framework
2. Continuous discovery task finds nearby peers
3. When peers are found, performance tests execute automatically
4. Results are logged and statistics reported periodically
5. System adapts to dynamic peer connections/disconnections

## Development Container

The project uses VS Code dev containers with ESP-IDF. The container includes:
- ESP-IDF toolchain and dependencies
- C/C++ development tools
- Serial monitoring capabilities
- Pre-configured build tasks

## Important Notes

- **Focus**: This is a **network testing** project, not a robot implementation
- **Hardware**: Requires minimum 3 ESP32-C5 boards for meaningful tests
- **Environment**: Designed for warehouse RF environment simulation
- **Mobility**: Tests dynamic peer discovery as devices move in/out of range
- **Limits**: Currently testing without artificial peer limits to find natural ESP-NOW capabilities

## Future Context

While this project tests networking fundamentals, the end goal is mobile robot coordination requiring:
- <10ms message latency for collision avoidance
- <500ms peer discovery for dynamic networks
- >99% reliability for safety-critical communications
- 30-50 meter range in warehouse environments

The test results will inform whether ESP-NOW on ESP32-C5 can meet these requirements.