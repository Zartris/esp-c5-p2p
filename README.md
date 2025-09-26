# ESP32-C5 P2P Development Project

A VS Code development environment setup for ESP32-C5 peer-to-peer communication applications using ESP-IDF.

## Prerequisites

- [Docker](https://www.docker.com/products/docker-desktop) installed on your system
- [VS Code](https://code.visualstudio.com/) with the [Remote-Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)

## Getting Started

### 1. Setup Development Container

1. Open this project in VS Code
2. Press `F1` and select `Remote-Containers: Open Folder in Container...`
3. Select the project folder
4. VS Code will build the ESP-IDF development container (this may take a few minutes on first run)

### 2. Build and Flash

Once the container is running:

1. Open the VS Code terminal (`Ctrl+Shift+` ` or `View > Terminal`)
2. Build the project:
   ```bash
   idf.py build
   ```
3. Connect your ESP32-C5 development board(s) via USB
4. Flash the firmware:
   ```bash
   idf.py flash
   ```
5. Monitor the output:
   ```bash
   idf.py monitor
   ```

### Multi-Device Testing

For testing ESP-NOW communication between multiple devices:

#### Step 1: Find Connected Devices
```bash
# List all connected USB serial devices
ls /dev/tty* | grep -E "(ACM|USB)"

# Or check recent USB connections
dmesg | tail -20
```

#### Step 2: Flash Multiple Devices
```bash
# Flash first device (e.g., coordinator)
idf.py -p /dev/ttyACM0 flash

# Flash second device (e.g., peer)
idf.py -p /dev/ttyACM1 flash

# Or use USB ports if that's how they appear
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB1 flash
```

#### Step 3: Monitor Both Devices
Open **two separate terminals**:

**Terminal 1** (Monitor first device):
```bash
idf.py -p /dev/ttyACM0 monitor
```

**Terminal 2** (Monitor second device):
```bash
idf.py -p /dev/ttyACM1 monitor
```

#### Combined Commands
```bash
# Build, flash, and monitor specific device in one command
idf.py -p /dev/ttyACM0 build flash monitor

# Just flash and monitor
idf.py -p /dev/ttyACM1 flash monitor
```

**Note**: Replace `/dev/ttyACM0`, `/dev/ttyACM1` with the actual ports your devices connect to.

### 3. Using VS Code Tasks

You can also use the predefined VS Code tasks:

- **Build**: `Ctrl+Shift+P` → `Tasks: Run Task` → `Build`
- **Flash**: `Ctrl+Shift+P` → `Tasks: Run Task` → `Flash`
- **Monitor**: `Ctrl+Shift+P` → `Tasks: Run Task` → `Monitor`
- **Build, Flash and Monitor**: `Ctrl+Shift+P` → `Tasks: Run Task` → `Build, Flash and Monitor`

## Project Structure

```
esp-c5-p2p/
├── .devcontainer/
│   └── devcontainer.json          # Dev container configuration
├── .vscode/
│   ├── c_cpp_properties.json      # C/C++ IntelliSense configuration
│   ├── launch.json                # Debug configuration
│   ├── settings.json              # VS Code workspace settings
│   └── tasks.json                 # Build tasks
├── main/
│   ├── CMakeLists.txt             # Main component build file
│   └── main.c                     # Main application code
├── components/                     # Custom components (empty initially)
├── CMakeLists.txt                 # Root project CMake file
├── sdkconfig.defaults             # ESP32-C5 default configuration
└── README.md                      # This file
```

## ESP-IDF Extensions

The development container includes the following VS Code extensions:

- **ESP-IDF Extension**: Official Espressif extension for ESP-IDF development
- **C/C++ Extension Pack**: Microsoft C/C++ tools for IntelliSense and debugging
- **CMake Tools**: CMake integration
- **Python**: Python language support for ESP-IDF build system
- **Serial Monitor**: Monitor ESP32 serial output
- **Code Runner**: Quick code execution

## Configuration

### ESP32-C5 Specific Settings

The project is pre-configured for ESP32-C5 development:

- Target chip: ESP32-C5
- WiFi and Bluetooth enabled
- 4MB Flash size
- 80MHz Flash frequency
- DIO Flash mode

### Customization

You can customize the ESP-IDF configuration by running:

```bash
idf.py menuconfig
```

This will open the configuration menu where you can modify various ESP-IDF settings.

## Development Tips

1. **IntelliSense**: The project is configured with proper include paths for full ESP-IDF IntelliSense support
2. **Debugging**: Use the preconfigured GDB setup for hardware debugging
3. **Serial Monitoring**: The Serial Monitor extension provides an easy way to view ESP32 output
4. **Port Configuration**: Update the serial port in `.vscode/settings.json` if needed

## Troubleshooting

### Container Build Issues
- Ensure Docker is running and you have sufficient disk space
- Try rebuilding the container: `Ctrl+Shift+P` → `Remote-Containers: Rebuild Container`

### Device Not Found
- Check USB cable connection
- Verify the correct serial port in settings
- Ensure ESP32-C5 drivers are installed on your host system

### Build Errors
- Clean the build: `idf.py clean`
- Check ESP-IDF version compatibility
- Verify all dependencies are properly installed in the container

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is licensed under the MIT License - see the LICENSE file for details.