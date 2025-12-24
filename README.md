# A.R.I.S.E. - Advanced Recognition & Intelligent Scanning Ecosystem

**Professional Fingerprint Attendance System with Battery Protection**

A robust, feature-complete biometric attendance system built on ESP32 with fingerprint authentication, WiFi connectivity, and intelligent power management.

## 🌟 Key Features

- **Universal Fingerprint Support**: Compatible with AS608 & R307 fingerprint sensors
- **Smart Battery Management**: Real-time voltage monitoring with calibrated protection
- **Dual Power Mode**: USB-powered operation or battery-powered portable use
- **Offline Capability**: Queue system for network failures with automatic sync
- **OLED Status Display**: Real-time WiFi, battery, and queue information
- **Admin Interface**: Serial command system for enrollment and management
- **Heartbeat Monitoring**: Regular device status reporting to server
- **Factory Reset Protection**: Safe configuration management

## 📁 Repository Structure

```
A.R.I.S.E-Firmware/
├── sketch.ino              # Main firmware file
├── UniversalFingerprint.h  # Sensor abstraction layer
├── UniversalFingerprint.cpp # Sensor implementation
└── README.md              # This documentation
```

## 🔧 Hardware Requirements

- **ESP32 Development Board** (WiFi/Bluetooth)
- **Fingerprint Sensor** (AS608 or R307 recommended)
- **OLED Display** (SSD1306, 128x64, I2C)
- **Li-Po Battery** (3.7V, 1000mAh+ recommended)
- **Voltage Divider Circuit** (2x 10kΩ resistors)
- **USB Power Supply** (for USB-powered operation)

## 🚀 Quick Start

### 1. Hardware Connections

| ESP32 Pin | Component | Connection |
|-----------|-----------|------------|
| GPIO 21 (SDA) | OLED Display | SDA |
| GPIO 22 (SCL) | OLED Display | SCL |
| GPIO 16 (RX2) | Fingerprint Sensor | TX |
| GPIO 17 (TX2) | Fingerprint Sensor | RX |
| GPIO 35 | Battery Monitor | Voltage Divider Output |
| 3.3V | All Components | VCC |
| GND | All Components | GND |

### 2. Firmware Upload

1. Install Arduino IDE with ESP32 board support
2. Install required libraries:
   - Adafruit_GFX
   - Adafruit_SSD1306
   - Adafruit_Fingerprint
   - ArduinoJson
   - Preferences
3. Open `sketch.ino` in Arduino IDE
4. Select your ESP32 board
5. Upload the firmware

### 3. Initial Configuration

Connect via Serial Monitor (115200 baud) and configure:

```bash
# Set WiFi credentials
wifi YourNetworkName YourPassword

# Set server address
server http://192.168.1.100:5000

# Verify settings
config
```

## 📊 System Architecture

### Power Management
- **USB Detection**: Automatic mode switching
- **Battery Calibration**: Advanced voltage curve mapping
- **Critical Protection**: Safe shutdown at 3.3V
- **Percentage Calculation**: Precise 1% increments with hysteresis

### Network Stack
- **Persistent Storage**: WiFi credentials saved in NVS
- **Smart Reconnection**: Graceful offline operation
- **Heartbeat System**: Regular server communication
- **Queue Management**: Offline data retention with auto-sync

### Fingerprint System
- **Universal Driver**: AS608/R307 compatibility
- **Slot Management**: Occupancy checking and protection
- **Roll ID Mapping**: 2 fingerprints per student
- **Admin Tools**: Enrollment, deletion, and testing

## 🎮 Admin Commands

### Network Configuration
```bash
wifi SchoolWiFi MyPassword123      # Set WiFi credentials
server http://192.168.1.100:5000   # Set server address
config                             # Show current settings
reset-config                       # Factory reset
```

### Fingerprint Management
```bash
enroll 10                          # Enroll fingerprint in slot 10
delete 10                          # Delete fingerprint from slot 10
delete-all                         # Delete ALL fingerprints
match                              # Test fingerprint matching
sensor-info                       # Show sensor details
test-sensor                       # Run diagnostics
```

### Information
```bash
help                              # Show all commands
config                            # Current configuration
```

## 🔋 Battery Specifications

| Voltage Range | Percentage | Status |
|--------------|------------|---------|
| 4.18V+       | 100%       | Fully Charged |
| 3.70V-4.00V  | 30-85%     | Normal Operation |
| 3.40V-3.70V  | 1-30%      | Low Battery |
| 3.30V        | 0%         | Critical Shutdown |

**Note**: USB power bypasses battery protection for continuous operation.

## 🛠️ Troubleshooting

### Common Issues

1. **Sensor Not Detected**
   - Check wiring (RX/TX crossover)
   - Verify 57600 baud rate
   - Run `test-sensor` command

2. **WiFi Connection Failed**
   - Verify credentials with `config`
   - Check network availability
   - Use `reset-config` to clear settings

3. **Battery Percentage Inaccurate**
   - Run calibration: `collectCalibrationData()`
   - Adjust `VOLTAGE_CORRECTION_OFFSET`
   - Ensure proper voltage divider values

4. **Display Issues**
   - Verify I2C address (0x3C)
   - Check SDA/SCL connections
   - Ensure proper power (3.3V)

### Debug Tools

```bash
# Enable verbose output in code:
# Uncomment Serial.print lines in:
# - readBatteryVoltage()
# - updateBatteryPercentage()
```

## 📈 Performance Metrics

- **Scan Time**: < 2 seconds (fingerprint to server)
- **Battery Life**: 24+ hours (standby with periodic scans)
- **Queue Capacity**: 50 offline records
- **Template Storage**: 162 (AS608) or 1000 (R307)
- **Update Rate**: Status bar updates every 2 seconds

## 🔒 Security Features

- **Slot Protection**: Prevents accidental overwrites
- **Factory Reset**: Requires explicit confirmation
- **Battery Safety**: Hardware-level protection
- **Network Security**: Encrypted WiFi connections

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Test thoroughly with hardware
4. Submit a pull request

## 📝 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🙏 Acknowledgments

- Adafruit Industries for fingerprint library
- ESP32 Arduino community
- Contributors to ArduinoJson and Adafruit_GFX libraries

---

**Repository Keywords**: `fingerprint-attendance`, `esp32`, `biometric`, `iot`, `arduino`, `wifi-attendance`, `battery-powered`, `offline-sync`, `oled-display`, `smart-scanner`, `educational-technology`, `portable-biometrics`

**Tags**: `#FingerprintScanner`, `#ESP32`, `#IoT`, `#AttendanceSystem`, `#Biometric`, `#Arduino`, `#BatteryManagement`, `#OfflineFirst`, `#EducationalTech`