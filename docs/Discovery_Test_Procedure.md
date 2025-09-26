# ESP-NOW Discovery Performance Test Procedure

## Test Objective
Measure the time required for Device A to discover Device B using ESP-NOW on 5GHz (Channel 36).

## Hardware Requirements
- 2x ESP32-C5 development boards
- 2x USB cables
- Computer with 2 available USB ports
- Development environment (VS Code with ESP-IDF container)

## Test Setup

### Step 1: Prepare Hardware
1. Connect **Device B** to computer via USB (leave powered on)
2. Have **Device A** ready but **UNPLUGGED** (this will be our test device)
3. Identify USB ports:
   ```bash
   ls /dev/tty* | grep -E "(ACM|USB)"
   ```
   - Device B will likely be `/dev/ttyACM0` or `/dev/ttyUSB0`
   - Device A will be the next available port when connected

### Step 2: Flash Both Devices
1. **Flash Device B** (baseline/reference device):
   ```bash
   idf.py -p /dev/ttyACM0 build flash
   ```

2. **Flash Device A** (test device):
   ```bash
   # Connect Device A temporarily
   idf.py -p /dev/ttyACM1 build flash
   # Disconnect Device A after flashing
   ```

### Step 3: Set Up Monitoring
1. **Start Device B monitoring** in Terminal 1:
   ```bash
   idf.py -p /dev/ttyACM0 monitor
   ```

2. **Prepare Device A monitoring** in Terminal 2 (don't run yet):
   ```bash
   idf.py -p /dev/ttyACM1 monitor
   ```

## Test Execution

### Step 4: Establish Baseline
1. **Device B should be running** and showing logs like:
   ```
   I (12345) main: ESP32-C5 ESP-NOW Discovery Test Device
   I (12346) main: DEVICE_MAC: aa:bb:cc:dd:ee:ff
   I (12347) main: DISCOVERY_STARTED!
   I (12348) main: STATUS: Actively searching for peers...
   ```

2. **Verify Device B is discoverable** - it should show periodic discovery activity

### Step 5: Discovery Timing Test
1. **Start Device A monitoring** in Terminal 2:
   ```bash
   idf.py -p /dev/ttyACM1 monitor
   ```

2. **Connect Device A power/USB** and immediately watch both terminals

3. **Record key timestamps from Device A**:
   - `BOOT_TIMESTAMP`: When device powered on
   - `DISCOVERY_START_TIMESTAMP`: When discovery began
   - `DISCOVERY_TIMESTAMP`: When first peer discovered

### Step 6: Data Collection
**Device A will automatically log discovery timing:**

```
I (1234) main: ========================================
I (1234) main: ESP32-C5 ESP-NOW Discovery Test Device
I (1234) main: BOOT_TIMESTAMP: 1234567890 us
I (1234) main: DEVICE_MAC: 11:22:33:44:55:66
I (1234) main: ========================================
...
I (5678) main: DISCOVERY_STARTED!
I (5678) main: DISCOVERY_START_TIMESTAMP: 1234572568 us
I (5678) main: INITIALIZATION_TIME: 4678.000 ms
I (5678) main: STATUS: Actively searching for peers...
I (5678) main: ========================================
...
I (7890) main: ========================================
I (7890) main: PEER_DISCOVERED!
I (7890) main: PEER_MAC: aa:bb:cc:dd:ee:ff
I (7890) main: PEER_RSSI: -45 dBm
I (7890) main: DISCOVERY_TIMESTAMP: 1234574890 us
I (7890) main: TIME_SINCE_BOOT: 7900.000 ms
I (7890) main: DISCOVERY_LATENCY: 2322.000 ms
I (7890) main: ========================================
```

## Key Metrics to Record

### Primary Measurements
1. **Discovery Latency**: Time from "DISCOVERY_STARTED" to "PEER_DISCOVERED"
   - Found in log as: `DISCOVERY_LATENCY: X.XXX ms`

2. **Total Boot-to-Discovery Time**: Time from power-on to first peer found
   - Found in log as: `TIME_SINCE_BOOT: X.XXX ms`

3. **Initialization Overhead**: Time from boot to discovery start
   - Found in log as: `INITIALIZATION_TIME: X.XXX ms`

### Secondary Measurements
4. **RSSI at Discovery**: Signal strength when peer discovered
   - Found in log as: `PEER_RSSI: -XX dBm`

5. **MAC Addresses**: Verify correct devices discovered
   - Device A MAC: `DEVICE_MAC: xx:xx:xx:xx:xx:xx`
   - Device B MAC: `PEER_MAC: xx:xx:xx:xx:xx:xx`

## Test Repetition

### Multiple Test Runs
1. **Disconnect Device A** (power off)
2. **Wait 10 seconds** for clean state
3. **Reconnect Device A** and monitor
4. **Repeat 10+ times** for statistical analysis

### Data Recording Template
```
Test Run | Discovery Latency (ms) | Total Time (ms) | Init Time (ms) | RSSI (dBm)
---------|----------------------|----------------|----------------|------------
1        | 2322.000             | 7900.000       | 4678.000       | -45
2        | 1856.000             | 6234.000       | 3890.000       | -42
3        | 2105.000             | 7654.000       | 4321.000       | -48
...      | ...                  | ...            | ...            | ...
```

## Expected Results

### Typical Performance Ranges
- **Discovery Latency**: 1000-5000 ms (1-5 seconds)
- **Initialization Time**: 3000-6000 ms (3-6 seconds)
- **RSSI**: -30 to -80 dBm (depending on distance)

### Success Criteria
- ✅ Discovery latency < 5000 ms consistently
- ✅ No failed discoveries (should always find Device B)
- ✅ RSSI values reasonable for test distance
- ✅ MAC addresses correct and consistent

## Troubleshooting

### No Discovery Occurring
- **Check 5GHz support**: Ensure both devices support 5GHz WiFi
- **Verify Channel 36**: Both devices should use same 5GHz channel
- **Check distance**: Move devices closer (< 5 meters for initial tests)
- **Restart Device B**: Ensure baseline device is discoverable

### Inconsistent Timing
- **USB power issues**: Use consistent power source
- **Environmental interference**: Test in different locations
- **Multiple discoveries**: Device A might discover Device B multiple times

### Log Analysis Issues
- **Terminal buffer**: Increase terminal scroll buffer to capture all logs
- **Timestamp synchronization**: Use Device A timestamps as primary reference
- **Log filtering**: Focus on lines containing "DISCOVERY_LATENCY" for key data

## Data Analysis

### Calculate Statistics
From multiple test runs, calculate:
- **Average discovery latency**
- **Minimum/Maximum discovery times**
- **Standard deviation** (consistency measure)
- **Success rate** (percentage of successful discoveries)

### Performance Baseline
This test establishes baseline ESP-NOW discovery performance for:
- Future optimization comparison
- Different distance testing
- Multi-device scaling analysis
- Environmental interference impact assessment

## Test Completion Checklist
- [ ] Both devices flashed with identical code
- [ ] Device B running and discoverable
- [ ] Device A test repeated 10+ times
- [ ] Discovery latency data recorded
- [ ] RSSI values documented
- [ ] Statistics calculated (average, min, max)
- [ ] Any anomalies or issues noted