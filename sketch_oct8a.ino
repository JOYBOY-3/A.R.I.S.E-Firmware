// A.R.I.S.E. Smart Scanner Firmware - Version 3.2 (Battery Protected)
// ===================================================================
// Professional fingerprint attendance system with battery protection
// Features: WiFi connectivity, OLED display, battery monitoring, deep sleep
// ===================================================================



// --- LIBRARY INCLUDES ---
// These libraries provide the functions for hardware components and networking
#include <Wire.h>                  // I2C communication for OLED display
#include <Adafruit_GFX.h>          // Graphics library for display
#include <Adafruit_SSD1306.h>      // OLED display driver
#include <WiFi.h>                  // WiFi connectivity
#include <HTTPClient.h>            // HTTP requests to server
#include <Adafruit_Fingerprint.h>  // Adafruit fingerprint library
#include "UniversalFingerprint.h"  // Custom Fingerprint Library for AS608 and R307
#include <ArduinoJson.h>           // JSON parsing for server communication
#include <Preferences.h>           // Use preference for wifi configuration
Preferences preferences;




// --- HARDWARE DEFINITIONS ---
// Define pins and setup for fingerprint sensor and OLED display
HardwareSerial sensorSerial(2);  // Use Serial2 for fingerprint sensor (pins 16, 17)
UniversalFingerprint finger(&sensorSerial);




// OLED Display configuration for 128x64 0.96 inch screen
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin (-1 if not used)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);




// --- BATTERY MONITORING CONFIGURATION ---
#include "esp_adc_cal.h"
#include "driver/adc.h"

#define BATTERY_PIN 35                       // GPIO pin connected to voltage divider
const float R1 = 9650.0;                     // First resistor in voltage divider (10kΩ)
const float R2 = 9650.0;                     // Second resistor in voltage divider (10kΩ)
const float DIVIDER_RATIO = R2 / (R1 + R2);  // Should be 0.5 (50% division)

const float VOLTAGE_CORRECTION_SLOPE = 1.0;
const float VOLTAGE_CORRECTION_OFFSET = 0.21;

// ADC Calibration
esp_adc_cal_characteristics_t adc_chars;
bool adc_calibrated = false;

const float LOW_BATTERY_VOLTAGE = 3.3;  // Complete shutdown at 3.3V (calibrated)

// Global variables for battery status
float batteryVoltage = 0.0;      // Current battery voltage
int batteryPercentage = 0;       // Battery percentage (0-100%)
bool isBatteryCritical = false;  // Flag for critical battery state

// ✅ ADD: USB Power Detection
bool isUSBPowered() {
  // USB power typically provides >4.5V, battery max is 4.2V
  return (batteryVoltage > 4.5);
}

// ✅ ADD: Non-blocking battery check variables
unsigned long lastBatteryCheck = 0;
const long BATTERY_CHECK_INTERVAL = 2000;  // Check every 2 seconds instead of every loop


// --- NETWORK CONFIGURATION ---
char ssid[32] = "JOY_BOY 8384";                    // Your WiFi SSID
char password[32] = "833G47j,";                    // Your WiFi password
char server_ip[64] = "http://192.168.137.1:5000";  // Server IP address


// --- STATE MANAGEMENT & TIMERS ---
bool isSessionActive = false;           // Is attendance session active?
String activeSessionName = "";          // Name of active session
unsigned long lastStatusCheck = 0;      // Last time server status was checked
const long statusCheckInterval = 5000;  // Check server every 5 seconds
unsigned long lastHeartbeat = 0;        // Last heartbeat sent to server
const long heartbeatInterval = 10000;   // Send heartbeat every 10 seconds




// --- OFFLINE QUEUE FOR NETWORK FAILURES ---
#define MAX_QUEUE_SIZE 50          // Maximum queue size
int offlineQueue[MAX_QUEUE_SIZE];  // Array to store roll IDs when offline
int queueCount = 0;                // Number of items in queue
int syncCount = 0;                 // Number of successfully synced items



// --- ADMIN TASK MANAGEMENT ---
enum AdminTask { NONE,
                 ENROLL,
                 DELETE,
                 MATCH,
                 DELETE_ALL };      // Possible admin tasks
AdminTask currentAdminTask = NONE;  // Current active admin task
int adminTaskId = 0;                // ID for admin task (slot number)




// =============================================
// BATTERY & DISPLAY FUNCTIONS
// =============================================


/**
 * Initialize and calibrate ESP32 ADC for accurate voltage reading
 */
void setupADCCalibration() {
  // Configure ADC parameters for GPIO35 (ADC1_CHANNEL_7)
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);

  // Characterize ADC with Vref = 1100mV (typical for ESP32)
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1,
                                                          ADC_ATTEN_DB_11,
                                                          ADC_WIDTH_BIT_12,
                                                          1100,
                                                          &adc_chars);

  // Check characterization type
  Serial.println("=== ADC CALIBRATION ===");
  switch (val_type) {
    case ESP_ADC_CAL_VAL_EFUSE_VREF:
      Serial.println("ADC calibrated using eFuse Vref");
      break;
    case ESP_ADC_CAL_VAL_EFUSE_TP:
      Serial.println("ADC calibrated using eFuse Two Point");
      break;
    default:
      Serial.println("ADC calibrated using default Vref (1100mV)");
      break;
  }

  adc_calibrated = true;
  Serial.println("ADC calibration complete!");
}


/**
 * Floating point mapping function for precise percentage calculation
 * @param x: Input value to map
 * @param in_min: Minimum input range
 * @param in_max: Maximum input range  
 * @param out_min: Minimum output range
 * @param out_max: Maximum output range
 * @return: Mapped floating point value
 */
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}



void loadAllConfig() {
  preferences.begin("device-config", true);  // Read-only mode

  // Load WiFi credentials
  if (preferences.getString("ssid", "").length() > 0) {
    String savedSSID = preferences.getString("ssid");
    String savedPass = preferences.getString("password");
    savedSSID.toCharArray(ssid, 32);
    savedPass.toCharArray(password, 32);
  }

  // ✅ ADDED: Load server configuration
  if (preferences.getString("server_ip", "").length() > 0) {
    String savedServerIP = preferences.getString("server_ip");
    savedServerIP.toCharArray(server_ip, 64);
    Serial.println("Loaded server: " + savedServerIP);
  }

  preferences.end();
}

void saveWiFiConfig(const char* newSSID, const char* newPassword) {
  preferences.begin("device-config", false);  // Read-write mode
  preferences.putString("ssid", newSSID);
  preferences.putString("password", newPassword);
  preferences.end();
  Serial.println("WiFi credentials saved.");
}

// Save server configuration
void saveServerConfig(const char* newServerIP) {
  preferences.begin("device-config", false);
  preferences.putString("server_ip", newServerIP);
  preferences.end();
  Serial.println("Server configuration saved: " + String(newServerIP));
}



// Serial command handler
void handleConfigCommand(String command) {
  command.trim();

  // WiFi Configuration
  if (command.startsWith("wifi ")) {
    int firstSpace = command.indexOf(' ');
    int secondSpace = command.indexOf(' ', firstSpace + 1);

    if (firstSpace != -1 && secondSpace != -1) {
      String newSSID = command.substring(firstSpace + 1, secondSpace);
      String newPassword = command.substring(secondSpace + 1);

      if (newSSID.length() > 0 && newPassword.length() > 0) {
        saveWiFiConfig(newSSID.c_str(), newPassword.c_str());
        Serial.println("✅ WiFi settings saved!");
        Serial.println("📶 Restart device to connect to: " + newSSID);
      } else {
        Serial.println("❌ Please provide both WiFi name and password");
      }
    } else {
      Serial.println("❌ Usage: wifi NetworkName Password");
      Serial.println("   Example: wifi SchoolWiFi MyPassword123");
    }
  }

  // Server Configuration
  else if (command.startsWith("server ")) {
    String newServerIP = command.substring(7);
    newServerIP.trim();

    if (newServerIP.length() > 0 && (newServerIP.startsWith("http://") || newServerIP.startsWith("https://"))) {
      saveServerConfig(newServerIP.c_str());
      Serial.println("✅ Server address saved!");
      Serial.println("🌐 New server: " + newServerIP);
    } else {
      Serial.println("❌ Server address must start with http:// or https://");
      Serial.println("   Example: server http://192.168.1.100:5000");
    }
  }

  // Show current configuration
  else if (command == "config") {
    Serial.println("\n⚙️ CURRENT SETTINGS:");
    Serial.println("📶 WiFi: " + String(ssid));
    Serial.println("🌐 Server: " + String(server_ip));
    Serial.println("💡 Use 'wifi' or 'server' commands to change");
  }

  // Reset to defaults
  else if (command == "reset-config") {
    preferences.begin("device-config", false);
    preferences.clear();
    preferences.end();
    Serial.println("✅ Settings reset to factory defaults");
    Serial.println("🔄 Restart device to apply changes");
  }

  else {
    Serial.println("❌ Unknown command. Type anything to see available commands.");
  }
}





// =============================================
// STATUS BAR FUNCTIONS (ALWAYS VISIBLE - TOP ROW)
// =============================================


/**
 * Draws WiFi signal strength indicator with rectangle bars
 * @param x: X position on display
 * @param y: Y position on display
 *
 * Displays: 4 vertical bars representing signal strength
 * 4 bars = excellent, 0 bars = poor/disconnected
 */
void drawWifiStrengthBar(int x, int y) {
  long rssi = WiFi.RSSI();  // Get WiFi signal strength (more negative = weaker)
  int bars = 0;             // Number of bars to display (0-4)
  int wifiPercent = 0;      // WiFi signal percentage (0-100%)

  // Calculate bars based on RSSI value
  if (WiFi.status() == WL_CONNECTED) {
    if (rssi > -55) bars = 4;       // Excellent signal: -30 to -55 dBm
    else if (rssi > -65) bars = 3;  // Good signal: -55 to -65 dBm
    else if (rssi > -75) bars = 2;  // Fair signal: -65 to -75 dBm
    else if (rssi > -85) bars = 1;  // Weak signal: -75 to -85 dBm
    else bars = 0;                  // Very weak: -85 to -100 dBm

    // Convert RSSI to percentage for numerical display
    wifiPercent = map(constrain(rssi, -100, -30), -100, -30, 0, 100);
  }

  // Draw WiFi icon and bars using rectangles
  display.setCursor(x, y);

  // Draw 4 vertical bars representing signal strength
  int barWidth = 3;
  int barSpacing = 1;
  int barStartX = x;  // Start after "WiFi:" text


  for (int i = 0; i < 4; i++) {
    int barHeight = (i + 1) * 2;  // Heights: 2, 4, 6, 8 pixels
    int barX = barStartX + (i * (barWidth + barSpacing));
    int barY = y + 8 - barHeight;  // Bottom-aligned bars

    if (i < bars) {
      // Draw filled bar for good signal
      display.fillRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
    } else {
      // Draw outline for weak/no signal
      display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
    }
  }

  // Display percentage or "OFF" if disconnected
  display.setCursor(barStartX + 20, y);
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiPercent < 10) display.print(" ");  // Padding for single digit
    display.print(wifiPercent);
    display.print("% ");
  } else {
    display.print("OFF ");  // Show OFF when WiFi disconnected
  }
}




/**
 * Draws battery information with percentage and voltage
 * @param x: X position on display
 * @param y: Y position on display
 *
 * Displays: B: 85% 3.8v
 * Shows battery percentage and actual voltage
 */
void drawBatteryInfo(int x, int y) {
  display.setCursor(x, y);
  display.print("  B:");

  // Add padding for consistent formatting
  if (batteryPercentage < 100) display.print(" ");
  if (batteryPercentage < 10) display.print(" ");

  display.print(batteryPercentage);
  display.print("% ");
  display.print(batteryVoltage, 1);  // Display voltage with 1 decimal
  display.print("v");
}




/**
 * Draws queue and sync information
 * @param x: X position on display
 * @param y: Y position on display
 *
 * Displays: Q:2 S:5
 * Q = Number of pending sync items, S = Number of successfully synced items
 */
void drawQueueInfo(int x, int y) {
  display.setCursor(x, y);
  display.print("Queue: ");
  display.print(queueCount);
  display.print("   Sync: ");
  display.print(syncCount);
}




/**
 * Draws the complete status bar at top of display
 * This function is called every time the display updates
 *
 * Layout: WiFi Bars  BatteryInfo  QueueInfo
 * Example: WiFi: [####] 82%  B: 85% 3.8v  Q:2 S:5
 */
void drawStatusBar() {
  // Fixed positions for optimal use of 128 pixel width
  drawWifiStrengthBar(0, 0);  // Left: WiFi indicator (0-64 pixels)
  drawBatteryInfo(45, 0);     // Middle: Battery info (65-104 pixels)
  drawQueueInfo(0, 12);       // Right: Queue info (105-127 pixels)

  // Draw separator line below status bar
  display.drawLine(0, 20, 127, 20, SSD1306_WHITE);
}




// =============================================
// BATTERY MONITORING & PROTECTION FUNCTIONS
// =============================================




/**
* Reads and calculates actual battery voltage with higher precision
* @return: Actual battery voltage in volts with 3 decimal precision
 * Uses voltage divider to safely measure battery voltage
 * Formula: Vbattery = (ADC_reading / 4095) * 3.3 * (R1+R2)/R2
*/
float readBatteryVoltage() {
  if (!adc_calibrated) {
    setupADCCalibration();
  }

  // ✅ IMPROVED: Much better filtering
  uint32_t raw_sum = 0;
  const int samples = 128;  // Increased for maximum stability

  // Very quick sampling
  for (int i = 0; i < samples; i++) {
    raw_sum += adc1_get_raw(ADC1_CHANNEL_7);
  }

  uint32_t raw_avg = raw_sum / samples;

  // Convert to voltage at GPIO35 pin
  uint32_t voltage_at_pin_mv = esp_adc_cal_raw_to_voltage(raw_avg, &adc_chars);
  float voltage_at_pin = voltage_at_pin_mv / 1000.0;

  // Calculate battery voltage
  float calculated_battery_voltage = voltage_at_pin / DIVIDER_RATIO;

  // ✅ IMPROVED: Advanced noise filtering
  static float voltage_readings[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  static int reading_index = 0;

  // Store new reading
  voltage_readings[reading_index] = calculated_battery_voltage;
  reading_index = (reading_index + 1) % 10;

  // Sort and remove outliers (median filter)
  float temp[10];
  for (int i = 0; i < 10; i++) temp[i] = voltage_readings[i];

  // Simple bubble sort
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (temp[i] > temp[j]) {
        float swap = temp[i];
        temp[i] = temp[j];
        temp[j] = swap;
      }
    }
  }

  // Take median of middle 6 values (ignore 2 highest and 2 lowest)
  float filtered_voltage = 0;
  for (int i = 2; i < 8; i++) {
    filtered_voltage += temp[i];
  }
  filtered_voltage /= 6.0;

  // ✅ APPLY CALIBRATION
  float calibrated_voltage = (filtered_voltage * VOLTAGE_CORRECTION_SLOPE) + VOLTAGE_CORRECTION_OFFSET;

  // Debug info
  // Serial.print("Battery: ");
  // Serial.print(calibrated_voltage, 3);
  // Serial.println("V");

  return calibrated_voltage;
}


// ✅ ADD: Calibration helper function
void collectCalibrationData() {
  // Serial.println("\n🔋 BATTERY CALIBRATION MODE");
  // Serial.println("============================");
  // Serial.println("1. Measure battery with multimeter");
  // Serial.println("2. Note the ACTUAL voltage");
  // Serial.println("3. Compare with ESP32 reading below");
  // Serial.println("4. We'll calculate correction factors");
  // Serial.println("============================\n");

  // Take 10 readings and average them
  float voltage_sum = 0;
  int readings = 10;

  for (int i = 0; i < readings; i++) {
    float v = readBatteryVoltage();
    voltage_sum += v;
    delay(500);
  }

  float average_voltage = voltage_sum / readings;

  // Serial.println("=== CALIBRATION RESULTS ===");
  // Serial.print("ESP32 Average Reading: ");
  // Serial.print(average_voltage, 3);
  // Serial.println("V");
  // Serial.println("Now measure with multimeter and compare!");
  // Serial.println("If different, we'll apply correction factors.");
  // Serial.println("============================\n");
}



/**
 * Update battery percentage with precise 1% increments
 * Advanced mapping with voltage curve compensation
 */
void updateBatteryPercentage() {
  batteryVoltage = readBatteryVoltage();

  // ✅ IMPROVED: Stable percentage with hysteresis and smoothing
  static int last_stable_percentage = -1;
  static unsigned long last_percentage_change = 0;
  const unsigned long MIN_CHANGE_TIME = 30000;  // 30 seconds between changes

  // Calculate raw percentage from voltage (more accurate curve)
  float raw_percentage = 0.0;

  // ✅ CALIBRATED: Lithium battery voltage-percentage curve
  if (batteryVoltage >= 4.18) {
    raw_percentage = 100.0;
  } else if (batteryVoltage >= 4.10) {
    raw_percentage = mapFloat(batteryVoltage, 4.10, 4.18, 95.0, 100.0);
  } else if (batteryVoltage >= 4.05) {
    raw_percentage = mapFloat(batteryVoltage, 4.05, 4.10, 90.0, 95.0);
  } else if (batteryVoltage >= 4.00) {
    raw_percentage = mapFloat(batteryVoltage, 4.00, 4.05, 85.0, 90.0);
  } else if (batteryVoltage >= 3.95) {
    raw_percentage = mapFloat(batteryVoltage, 3.95, 4.00, 80.0, 85.0);
  } else if (batteryVoltage >= 3.90) {
    raw_percentage = mapFloat(batteryVoltage, 3.90, 3.95, 70.0, 80.0);
  } else if (batteryVoltage >= 3.85) {
    raw_percentage = mapFloat(batteryVoltage, 3.85, 3.90, 60.0, 70.0);
  } else if (batteryVoltage >= 3.80) {
    raw_percentage = mapFloat(batteryVoltage, 3.80, 3.85, 50.0, 60.0);
  } else if (batteryVoltage >= 3.75) {
    raw_percentage = mapFloat(batteryVoltage, 3.75, 3.80, 40.0, 50.0);
  } else if (batteryVoltage >= 3.70) {
    raw_percentage = mapFloat(batteryVoltage, 3.70, 3.75, 30.0, 40.0);
  } else if (batteryVoltage >= 3.65) {
    raw_percentage = mapFloat(batteryVoltage, 3.65, 3.70, 20.0, 30.0);
  } else if (batteryVoltage >= 3.60) {
    raw_percentage = mapFloat(batteryVoltage, 3.60, 3.65, 10.0, 20.0);
  } else if (batteryVoltage >= 3.50) {
    raw_percentage = mapFloat(batteryVoltage, 3.50, 3.60, 5.0, 10.0);
  } else if (batteryVoltage >= 3.40) {
    raw_percentage = mapFloat(batteryVoltage, 3.40, 3.50, 1.0, 5.0);
  } else {
    raw_percentage = 0.0;
  }

  int new_percentage = (int)(raw_percentage + 0.5);
  new_percentage = constrain(new_percentage, 0, 100);

  // ✅ HYSTERESIS: Only update percentage under certain conditions
  unsigned long currentTime = millis();

  if (last_stable_percentage == -1) {
    // First time initialization
    batteryPercentage = new_percentage;
    last_stable_percentage = new_percentage;
  } else if (new_percentage == 0 || new_percentage == 100) {
    // Always update 0% and 100%
    batteryPercentage = new_percentage;
    last_stable_percentage = new_percentage;
    last_percentage_change = currentTime;
  } else if (abs(new_percentage - last_stable_percentage) >= 2) {
    // Only change if difference is 1% or more
    batteryPercentage = new_percentage;
    last_stable_percentage = new_percentage;
    last_percentage_change = currentTime;
  } else if ((currentTime - last_percentage_change) > MIN_CHANGE_TIME) {
    // Force update after 30 seconds even if small change
    batteryPercentage = new_percentage;
    last_stable_percentage = new_percentage;
    last_percentage_change = currentTime;
  }
  // Otherwise, keep the previous stable percentage

  // Debug info
  // static unsigned long last_debug = 0;
  // if (currentTime - last_debug > 5000) {
  //     Serial.print("Voltage: ");
  //     Serial.print(batteryVoltage, 3);
  //     Serial.print("V -> Raw %: ");
  //     Serial.print(raw_percentage, 1);
  //     Serial.print(" -> Stable %: ");
  //     Serial.println(batteryPercentage);
  //     last_debug = currentTime;
  // }
}


// ✅ ADD: Percentage calibration helper
void batteryPercentageCalibration() {
  // Serial.println("\n📊 BATTERY PERCENTAGE CALIBRATION");
  // Serial.println("=================================");
  // Serial.println("Voltage -> Percentage Mapping:");
  // Serial.println("=================================");

  // Test different voltage points
  float test_voltages[] = { 4.20, 4.15, 4.10, 4.05, 4.00, 3.95, 3.90, 3.85,
                            3.80, 3.75, 3.70, 3.65, 3.60, 3.50, 3.40, 3.30 };

  for (int i = 0; i < 16; i++) {
    float volt = test_voltages[i];

    // Simulate the percentage calculation
    float percentage = 0.0;
    if (volt >= 4.18) percentage = 100.0;
    else if (volt >= 4.10) percentage = mapFloat(volt, 4.10, 4.18, 95.0, 100.0);
    else if (volt >= 4.05) percentage = mapFloat(volt, 4.05, 4.10, 90.0, 95.0);
    else if (volt >= 4.00) percentage = mapFloat(volt, 4.00, 4.05, 85.0, 90.0);
    else if (volt >= 3.95) percentage = mapFloat(volt, 3.95, 4.00, 80.0, 85.0);
    else if (volt >= 3.90) percentage = mapFloat(volt, 3.90, 3.95, 70.0, 80.0);
    else if (volt >= 3.85) percentage = mapFloat(volt, 3.85, 3.90, 60.0, 70.0);
    else if (volt >= 3.80) percentage = mapFloat(volt, 3.80, 3.85, 50.0, 60.0);
    else if (volt >= 3.75) percentage = mapFloat(volt, 3.75, 3.80, 40.0, 50.0);
    else if (volt >= 3.70) percentage = mapFloat(volt, 3.70, 3.75, 30.0, 40.0);
    else if (volt >= 3.65) percentage = mapFloat(volt, 3.65, 3.70, 20.0, 30.0);
    else if (volt >= 3.60) percentage = mapFloat(volt, 3.60, 3.65, 10.0, 20.0);
    else if (volt >= 3.50) percentage = mapFloat(volt, 3.50, 3.60, 5.0, 10.0);
    else if (volt >= 3.40) percentage = mapFloat(volt, 3.40, 3.50, 1.0, 5.0);
    else percentage = 0.0;

    // Serial.print(volt, 2);
    // Serial.print("V -> ");
    // Serial.print((int)(percentage + 0.5));
    // Serial.println("%");
  }
  Serial.println("=================================\n");
}



// =============================================
// OLED DISPLAY FUNCTIONS
// =============================================




/**
 * Updates the OLED display with status bar and content
 * @param mainMsg: Primary message to display (center screen)
 * @param subMsg: Secondary message to display (below main message)
 *
 * Display Layout:
 * Row 0: Status Bar (WiFi, Battery, Queue - ALWAYS VISIBLE)
 * Row 1: Separator line
 * Row 2+: Main content area
 */
void updateDisplay(String mainMsg = "", String subMsg = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // --- ALWAYS DRAW STATIC STATUS BAR AT TOP ---
  drawStatusBar();

  // --- Render Dynamic Content Below Status Bar ---
  int contentStartY = 23;  // Start below status bar (row 10 + 2 pixel gap)

  // Display main message with appropriate text size
  if (mainMsg != "") {
    // Use larger text for short messages, smaller for long messages
    if (mainMsg.length() <= 8) {
      display.setTextSize(2);
      display.setCursor(0, contentStartY);
      display.println(mainMsg);
      contentStartY += 20;  // Move down for next element
    } else {
      display.setTextSize(2);
      display.setCursor(0, contentStartY);

      // Split long messages into multiple lines if needed
      String firstLine = mainMsg;
      String secondLine = "";

      if (mainMsg.length() > 16) {
        int splitPos = mainMsg.lastIndexOf(' ', 16);
        if (splitPos == -1) splitPos = 16;
        firstLine = mainMsg.substring(0, splitPos);
        secondLine = mainMsg.substring(splitPos + 1);
      }

      display.println(firstLine);
      contentStartY += 10;

      if (secondLine != "") {
        display.setCursor(0, contentStartY);
        display.println(secondLine);
        contentStartY += 10;
      }
    }
  }

  // Display sub message below main message
  if (subMsg != "") {
    display.setTextSize(1);
    display.setCursor(0, (contentStartY + 22));
    display.println(subMsg);
  }

  display.display();  // Send buffer to display
}



/*
* Checks battery level and takes protective action if needed
* Uses ACTUAL calibrated battery voltage
*/
void checkBatterySafety() {
  // ✅ CHANGED: Skip battery protection when USB powered
  if (isUSBPowered()) {
    return;  // Exit immediately if USB powered
  }

  updateBatteryPercentage();

  // CRITICAL: Battery at or below 3.2V - COMPLETE SHUTDOWN
  if (batteryVoltage <= LOW_BATTERY_VOLTAGE) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("BATTERY CRITICAL");
    display.setCursor(0, 35);
    display.println("SHUTTING DOWN...");
    display.setCursor(0, 50);
    display.print("Voltage: ");
    display.print(batteryVoltage, 2);
    display.println("V");
    display.display();

    delay(3000);  // Give user 5 seconds to read the message

    // COMPLETE SHUTDOWN
    // Turn off all peripherals first
    display.clearDisplay();
    display.display();
    delay(100);

    // For complete shutdown, we use the lowest power mode
    // Note: ESP32 doesn't have true "off" but this is the closest
    esp_deep_sleep_start();  // This will sleep forever until external reset
  }
}


// Non-blocking battery update function
void updateBatteryNonBlocking() {
  if (millis() - lastBatteryCheck > BATTERY_CHECK_INTERVAL) {
    updateBatteryPercentage();
    checkBatterySafety();
    lastBatteryCheck = millis();
  }
}








// =============================================
// UPDATED FINGERPRINT FUNCTIONS
// =============================================



/**
 * Universal fingerprint matching function
 */
/**
 * Universal fingerprint matching function with exact display messages
 */
/**
 * Universal fingerprint matching function - FIXED: No timeout messages during session
 */
int getFingerprintID() {
  Serial.println("\n=== UNIVERSAL FINGERPRINT MATCHING ===");

  // ✅ FIXED: Show initial message
  updateDisplay("Place Finger", activeSessionName);

  unsigned long startTime = millis();
  uint8_t p = FINGERPRINT_NOFINGER;

  // Step 1: Get Image with timeout - IMPROVED LOGIC
  while (true) {
    p = finger.getImage();

    if (p == FINGERPRINT_OK) {
      Serial.println("SUCCESS: Finger image captured");
      
      // ✅ FIXED: Show scanning status
      updateDisplay("Scanning", "Please wait...");
      break;
    } else if (p == FINGERPRINT_NOFINGER) {
      // ✅ FIXED: Continue waiting silently without showing timeout
      // Just check if we should return due to timeout, but don't show message
      if (millis() - startTime > 10000) {
        Serial.println("INFO: Finger detection timeout (normal during session)");
        return -1; // Return timeout but don't show message
      }
    } else {
      // Imaging error
      Serial.println("ERROR: Imaging error - code: " + String(p));
      updateDisplay("Scan Error", "Try again");
      delay(1000);
      return -2;
    }
    delay(100);
  }

  // Step 2: Convert image to template
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("ERROR: Failed to convert image to template");
    updateDisplay("Processing", "Error - Retry");
    delay(1000);
    return -2;
  }

  // Step 3: Search database
  p = finger.fingerFastSearch();

  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("SUCCESS: Match found!");
      Serial.println("-> Finger ID: " + String(finger.getFingerID()));
      Serial.println("-> Confidence: " + String(finger.getConfidence()));
      
      // ✅ FIXED: Clear success message
      updateDisplay("Verified", "Success!");
      delay(500);
      return finger.getFingerID();

    case FINGERPRINT_NOTFOUND:
      Serial.println("INFO: No match found in database");
      // ✅ FIXED: Show no match message briefly
      updateDisplay("Not Found", "In database");
      delay(1500);
      return -1;

    default:
      Serial.println("ERROR: Search failed: " + String(p));
      updateDisplay("Match Error", "Try again");
      delay(1000);
      return -2;
  }
}

/**
 * Checks if a fingerprint slot is already occupied with display feedback
 * @param id: Slot ID to check (1 to maxCapacity)
 * @return: true if slot is occupied, false if empty
 */
bool isSlotOccupied(int id) {
  Serial.println("Checking slot " + String(id) + " for existing enrollment...");

  // Don't show display message here to avoid duplicates
  // updateDisplay("Checking Slot", "ID: " + String(id));
  // delay(1000);

  // Try to load template from the slot
  uint8_t result = finger.loadModel(id);

  if (result == FINGERPRINT_OK) {
    Serial.println("⚠️  Slot " + String(id) + " is already occupied!");
    // Don't show display message here - let caller handle it
    return true;
  } else if (result == FINGERPRINT_PACKETRECIEVEERR) {
    // This might indicate communication error, but often means empty slot
    Serial.println("✅ Slot " + String(id) + " appears to be empty");
    // Don't show display message here - let caller handle it
    return false;
  } else {
    // Other errors - assume slot is empty for safety
    Serial.println("ℹ️  Slot " + String(id) + " check result: " + String(result) + " - treating as empty");
    // Don't show display message here - let caller handle it
    return false;
  }
}

/**
 * Enhanced enrollment function with overwrite protection
 */
int8_t getFingerprintEnroll(int id) {
  Serial.println("\n=== ENHANCED ENROLLMENT WITH OVERWRITE PROTECTION ===");

  // Check if slot is within sensor capacity
  uint16_t maxCapacity = finger.getMaxCapacity();
  if (id > maxCapacity) {
    Serial.println("❌ ERROR: Slot " + String(id) + " exceeds sensor capacity (" + String(maxCapacity) + ")");
    updateDisplay("Invalid Slot", "Max: " + String(maxCapacity));
    delay(3000);
    return FINGERPRINT_BADLOCATION;
  }

  // ✅ NEW: Check if slot is already occupied
  if (isSlotOccupied(id)) {
    Serial.println("❌ ENROLLMENT BLOCKED: Slot " + String(id) + " already has a fingerprint!");

    // ✅ UPDATED DISPLAY MESSAGES
    updateDisplay("Slot Occupied!", "ID: " + String(id));
    delay(2000);

    // Show existing roll ID mapping on display
    int existing_roll_id = floor((id - 1) / 2) + 1;
    updateDisplay("Maps to Roll", "#" + String(existing_roll_id));
    delay(2000);

    updateDisplay("Delete First", "Use: delete " + String(id));
    delay(2000);

    Serial.println("💡 This slot corresponds to Roll ID: " + String(existing_roll_id));
    Serial.println("💡 Use 'delete " + String(id) + "' to remove existing enrollment first");

    return FINGERPRINT_BADLOCATION;
  }

  Serial.println("✅ Slot " + String(id) + " is available for enrollment");
  // ✅ UPDATED DISPLAY MESSAGE
  updateDisplay("Slot Available", "ID: " + String(id));
  delay(1500);

  // Show roll ID mapping before starting enrollment
  int roll_id = floor((id - 1) / 2) + 1;
  updateDisplay("Will map to", "Roll #" + String(roll_id));
  delay(1500);

  // Continue with original enrollment process...
  int p = -1;
  updateDisplay("Enrolling", "Place finger...");
  Serial.println("Waiting for valid finger to enroll in slot " + String(id));

  unsigned long startTime = millis();
  const unsigned long timeout = 30000;

  // Step 1: Capture first fingerprint image
  while (true) {
    p = finger.getImage();

    if (p == FINGERPRINT_OK) {
      updateDisplay("Enrolling", "Image taken...");
      delay(1500);
      break;
    } else if (p == FINGERPRINT_NOFINGER) {
      // Continue waiting
      if (millis() - startTime > timeout) {
        updateDisplay("Enroll Timeout", "Please try again");
        Serial.println("Enrollment timeout - no finger detected");
        return FINGERPRINT_TIMEOUT;
      }
    } else {
      Serial.println("Imaging error: " + String(p));
      // Don't break, keep trying
    }
    delay(100);
  }

  // Convert first image to template
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    updateDisplay("Enroll Error", "Convert 1 fail");
    delay(1500);
    Serial.println("Error converting first image");
    return p;
  }

  updateDisplay("Enrolling", "Remove finger");
  delay(1500);

  // Wait for finger removal
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
    delay(100);
  }

  // Step 2: Capture second fingerprint image
  updateDisplay("Enrolling", "Place same finger");
  p = -1;
  startTime = millis();

  while (true) {
    p = finger.getImage();

    if (p == FINGERPRINT_OK) {
      updateDisplay("Enrolling", "Image taken...");
      delay(1500);
      break;
    } else if (p == FINGERPRINT_NOFINGER) {
      if (millis() - startTime > timeout) {
        updateDisplay("Enroll Timeout", "Please try again");
        Serial.println("Second image timeout");
        return FINGERPRINT_TIMEOUT;
      }
    } else {
      Serial.println("Second image error: " + String(p));
    }
    delay(100);
  }

  // Convert second image to template
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    updateDisplay("Enroll Error", "Convert 2 fail");
    delay(1500);
    Serial.println("Error converting second image");
    return p;
  }

  // Create fingerprint model
  updateDisplay("Enrolling", "Processing...");
  delay(1500);
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    updateDisplay("Enroll Error", "Prints mismatch");
    delay(1500);
    Serial.println("Fingerprints did not match");
    return p;
  }

  // Store model
  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    updateDisplay("Enroll Error", "Store failed");
    delay(1500);
    Serial.println("Error storing model");
    return p;
  }

  // Calculate class roll ID
  int class_roll_id = floor((id - 1) / 2) + 1;
  updateDisplay("Enrolled!", "Roll #" + String(class_roll_id));
  delay(1500);
  updateDisplay("Success!", "Slot #" + String(id));
  delay(1500);

  Serial.println("✅ SUCCESS: Fingerprint enrolled in slot " + String(id));
  Serial.println("📋 Mapping: Slot " + String(id) + " → Roll ID " + String(class_roll_id));
  return FINGERPRINT_OK;
}

/**
 * Updated database diagnostic function
 */
void checkFingerprintDatabase() {
  Serial.println("\n=== UNIVERSAL FINGERPRINT DATABASE ===");

  if (!finger.verifyPassword()) {
    Serial.println("ERROR: Fingerprint sensor not responding!");
    return;
  }

  Serial.println("SUCCESS: " + finger.getSensorName());
  Serial.println("-> Max Capacity: " + String(finger.getMaxCapacity()) + " templates");

  uint16_t templateCount = finger.getTemplateCount();
  if (templateCount > 0) {
    Serial.println("-> Templates stored: " + String(templateCount));
  }
}

/**
 * Emergency sensor reset function
 */
/**
 * Enhanced sensor reset with proper detection
 */
void resetFingerprintSensor() {
  Serial.println("Performing sensor hardware reset...");

  // Hardware reset - toggle power or use reset pin if available
  sensorSerial.end();
  delay(1000);
  sensorSerial.begin(57600, SERIAL_8N1, 16, 17);
  delay(2000);

  // Clear any garbage data
  while (sensorSerial.available()) {
    sensorSerial.read();
  }

  Serial.println("Re-initializing fingerprint sensor...");

  if (finger.begin()) {
    Serial.println("✅ Sensor reset successful!");
    Serial.println("Detected: " + finger.getSensorName());

    // Force AS608 detection if we know it's AS608
    Serial.println("Forcing AS608 compatibility mode...");
  } else {
    Serial.println("❌ Sensor reset failed!");
  }
}

/**
 * Delete ALL fingerprints from sensor memory (Factory Reset)
 */
bool deleteAllFingerprints() {
  Serial.println("\n=== DELETING ALL FINGERPRINTS ===");
  updateDisplay("Deleting ALL", "Please wait...");

  uint8_t p = finger.emptyDatabase();

  if (p == FINGERPRINT_OK) {
    Serial.println("SUCCESS: All fingerprints deleted from sensor memory");
    updateDisplay("All Deleted!", "Memory cleared");
    delay(2000);
    return true;
  } else {
    Serial.println("ERROR: Failed to delete all fingerprints. Error code: " + String(p));
    updateDisplay("Delete Failed", "Error: " + String(p));
    delay(2000);
    return false;
  }
}



// =============================================
// NETWORK COMMUNICATION FUNCTIONS
// =============================================


/**
 * Checks server for current session status
 * Updates isSessionActive and activeSessionName based on server response
 */
void checkServerSessionStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    isSessionActive = false;
    return;
  }

  HTTPClient http;
  http.setTimeout(10000);
  http.begin(String(server_ip) + "/api/session-status");

  int httpCode = http.GET();

  if (httpCode > 0) {

    if (httpCode == 200) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, http.getString());
      if (!error) {
        isSessionActive = doc["isSessionActive"];
        activeSessionName = doc["sessionName"].as<String>();
      } else {
        Serial.println("JSON parsing failed");
        isSessionActive = false;
      }
    }

  } else {
    isSessionActive = false;
    activeSessionName = "";
  }
  http.end();
}




/**
 * Sends attendance record to server
 * @param roll_id: Student roll ID to mark attendance for
 *
 * If offline, adds to queue for later sync
 * If online, sends immediately and processes response
 */
void sendToServer(int roll_id) {
  if (WiFi.status() != WL_CONNECTED) {
    addToQueue(roll_id); // Save offline if no connection
    return;
  }
 
  updateDisplay("Syncing...", "Roll #" + String(roll_id));
  delay(200);	
  HTTPClient http;
  http.begin(String(server_ip) + "/api/mark-attendance-by-roll-id");
  http.addHeader("Content-Type", "application/json");
 
  JsonDocument doc;
  doc["class_roll_id"] = roll_id;
  String jsonPayload;
  serializeJson(doc, jsonPayload);
 
  int httpCode = http.POST(jsonPayload);
 
  if (httpCode > 0) {
    JsonDocument responseDoc;
    deserializeJson(responseDoc, http.getString());
    String serverMsg = responseDoc["message"].as<String>();
    serverMsg.replace("\n", " ");
    String subMsg = "Roll #" + String(roll_id);
   
    // Handle different server responses
    if (responseDoc["status"] == "success") {
      updateDisplay("Marked     ", subMsg);
      delay(1000);
    } else if (responseDoc["status"] == "duplicate") {
      updateDisplay("!Already Marked", subMsg);
	    delay(1000);
    } else {
      updateDisplay(serverMsg, subMsg);
    	delay(1000);
    }
  } else {
    addToQueue(roll_id); // Save to queue on network error
  }
  http.end();
  delay(1000);
}





/**
 * Adds roll ID to offline queue for later synchronization
 * @param roll_id: Student roll ID to queue
 */
void addToQueue(int roll_id) {
  String subMsg = "Roll #" + String(roll_id);
  if (queueCount < MAX_QUEUE_SIZE) {
    offlineQueue[queueCount] = roll_id;
    queueCount++;
  }
  updateDisplay("Saved Offline", subMsg);
  delay(2000);
}




/**
 * Attempts to synchronize queued attendance records with server
 * Processes one item from the queue each call
 */
void tryToSyncQueue() {



  if (WiFi.status() != WL_CONNECTED || queueCount == 0) {
    return;
  }

  updateDisplay("Syncing Queue", String(queueCount) + " left...");
  delay(1000);
  int roll_id_to_sync = offlineQueue[0];  // Get first item in queue

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(String(server_ip) + "/api/mark-attendance-by-roll-id");
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["class_roll_id"] = roll_id_to_sync;
  String jsonPayload;
  serializeJson(doc, jsonPayload);

  int httpCode = http.POST(jsonPayload);

  // Remove from queue only on definitive server response
  if (httpCode == 200 || httpCode == 400 || httpCode == 409) {
    // Shift all queue items forward
    for (int i = 0; i < queueCount - 1; i++) {
      offlineQueue[i] = offlineQueue[i + 1];
    }
    queueCount--;
    syncCount++;
    Serial.println("Successfully synced roll #" + String(roll_id_to_sync));
  } else {
    Serial.println("Sync failed for roll #" + String(roll_id_to_sync));
  }
  http.end();
  delay(300);
}




/**
 * Sends device heartbeat to server with status information
 * Includes: MAC address, WiFi strength, battery, queue counts
 */
void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) { return; }

  HTTPClient http;
  http.begin(String(server_ip) + "/api/device/heartbeat");
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["mac_address"] = WiFi.macAddress();
  doc["wifi_strength"] = WiFi.RSSI();
  doc["battery"] = batteryPercentage;
  doc["queue_count"] = queueCount;
  doc["sync_count"] = syncCount;

  String jsonPayload;
  serializeJson(doc, jsonPayload);
  http.POST(jsonPayload);
  http.end();
}




// =============================================
// ADMIN SERIAL COMMAND INTERFACE
// =============================================



/**
 * Test basic fingerprint sensor functions
 */
void testFingerprintSensor() {
  Serial.println("\n=== FINGERPRINT SENSOR DIAGNOSTICS ===");

  // Test 1: Verify sensor connection
  Serial.print("1. Sensor connection: ");
  if (finger.verifyPassword()) {
    Serial.println("✅ OK");
  } else {
    Serial.println("❌ FAILED");
    return;
  }

  // Test 2: Get sensor info
  Serial.println("2. Sensor Info: " + finger.getSensorName());
  Serial.println("   Capacity: " + String(finger.getMaxCapacity()) + " templates");

  // Test 3: Test image capture
  Serial.print("3. Image capture test: ");
  updateDisplay("Testing", "Place finger...");

  uint8_t result = finger.getImage();
  if (result == FINGERPRINT_OK) {
    Serial.println("✅ SUCCESS - Finger detected");
  } else if (result == FINGERPRINT_NOFINGER) {
    Serial.println("⚠️  NO FINGER - This is normal if no finger placed");
  } else {
    Serial.println("❌ FAILED - Error: " + String(result));
  }

  delay(2000);

  // Test 4: Template count
  Serial.print("4. Template count: ");
  uint16_t templateCount = finger.getTemplateCount();
  Serial.println(String(templateCount) + " templates");

  Serial.println("=== DIAGNOSTICS COMPLETE ===");
}



/**
 * Handles serial commands from admin interface
 * Supported commands:
 * - enroll <id>: Enroll fingerprint in specified slot
 * - delete <id>: Delete fingerprint from specified slot  
 * - match: Test fingerprint matching
 */
/**
 * Handles serial commands from admin interface
 * Only shows help when explicitly requested or on unknown commands
 */
void handleSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    Serial.println("");  // Acknowledgment

    if (command.startsWith("wifi ") || command.startsWith("server ") || command == "config" || command == "reset-config") {
      handleConfigCommand(command);
    } else if (command == "test-sensor") {
      testFingerprintSensor();
    } else if (command.startsWith("enroll")) {
      adminTaskId = command.substring(7).toInt();
      uint16_t maxCapacity = finger.getMaxCapacity();

      if (adminTaskId > 0 && adminTaskId <= maxCapacity) {
        // ✅ FIXED: Only check occupancy, don't show display messages here
        if (isSlotOccupied(adminTaskId)) {
          int existing_roll_id = floor((adminTaskId - 1) / 2) + 1;
          Serial.println("❌ SLOT OCCUPIED: Slot " + String(adminTaskId) + " already enrolled!");

          // ✅ FIXED: Only show brief message, detailed messages will be in enrollment function
          updateDisplay("Slot Occupied", "ID: " + String(adminTaskId));
          delay(1500);

          Serial.println("💡 This slot maps to Roll ID: " + String(existing_roll_id));
          Serial.println("💡 Use 'delete " + String(adminTaskId) + "' first, then enroll again");
        } else {
          currentAdminTask = ENROLL;
          Serial.println("✅ Slot " + String(adminTaskId) + " is available");

          // ✅ FIXED: Only show brief confirmation, detailed messages in enrollment function
          updateDisplay("Starting", "Enrollment...");
          delay(1000);

          Serial.println("-> OK. Device is now waiting for a finger to enroll in slot " + String(adminTaskId));
          Serial.println("-> Sensor capacity: " + String(maxCapacity) + " templates");

          // Show roll ID mapping in serial only
          int roll_id = floor((adminTaskId - 1) / 2) + 1;
          Serial.println("📋 This slot will map to Roll ID: " + String(roll_id));
        }
      } else {
        Serial.println("❌ ERROR: Invalid ID. Must be 1-" + String(maxCapacity));
        updateDisplay("Invalid Slot", "Max: " + String(maxCapacity));
        delay(2000);
      }
    } else if (command.startsWith("delete-all")) {
      currentAdminTask = DELETE_ALL;
      Serial.println("-> OK. Deleting ALL fingerprints from sensor memory...");
    } else if (command.startsWith("delete")) {
      adminTaskId = command.substring(7).toInt();
      uint16_t maxCapacity = finger.getMaxCapacity();

      if (adminTaskId > 0 && adminTaskId <= maxCapacity) {
        currentAdminTask = DELETE;
        Serial.println("-> OK. Deleting template in slot " + String(adminTaskId));
      } else {
        Serial.println("--> ERROR: Invalid ID. Must be 1-" + String(maxCapacity));
      }
    } else if (command.startsWith("match")) {
      currentAdminTask = MATCH;
      Serial.println("-> OK. Device is now waiting for a finger to test matching.");
    } else if (command == "sensor-info") {
      Serial.println("\n=== SENSOR INFORMATION ===");
      Serial.println("Type: " + finger.getSensorName());
      Serial.println("Capacity: " + String(finger.getMaxCapacity()) + " templates");
      Serial.println("Status: " + String(finger.verifyPassword() ? "Connected" : "Disconnected"));
    } else if (command == "help" || command == "?") {
      showHelpMenu();
    } else if (command.length() > 0) {
      Serial.println("❌ Unknown command: '" + command + "'");
      Serial.println("💡 Type 'help' to see available commands");
    }
  }
}

void showHelpMenu() {
  uint16_t maxCapacity = finger.getMaxCapacity();

  Serial.println("\n📋 AVAILABLE COMMANDS:");
  Serial.println("\n--- WiFi & Server Setup ---");
  Serial.println("• wifi YourNetworkName YourPassword");
  Serial.println("• server http://your-server-ip:5000");
  Serial.println("• config                    (show current settings)");
  Serial.println("• reset-config              (reset to factory defaults)");

  Serial.println("\n--- Fingerprint Management ---");
  Serial.println("• enroll 5                  (save fingerprint in slot 5 of ROll #3");
  Serial.println("• delete 5                  (remove fingerprint from slot 5 of Roll #3");
  Serial.println("• delete-all                (delete ALL fingerprints)");
  Serial.println("• match                     (test fingerprint scanning)");
  Serial.println("• sensor-info               (show sensor details)");
  Serial.println("• help                      (show this help menu)");

  Serial.println("\n--- Examples ---");
  Serial.println("wifi SchoolWiFi MyPassword123");
  Serial.println("server http://192.168.1.100:5000");
  Serial.println("enroll 10");
  Serial.println("delete-all");
  Serial.println("sensor-info");
}




/**
 * Executes the current admin task
 * Called from main loop when currentAdminTask != NONE
 */
void executeAdminTask() {
  uint16_t maxCapacity = finger.getMaxCapacity();

  switch (currentAdminTask) {
    case ENROLL:
      {
        // Check if slot is within capacity
        if (adminTaskId > maxCapacity) {
          Serial.println("❌ ERROR: Slot " + String(adminTaskId) + " exceeds sensor capacity (" + String(maxCapacity) + ")");
          break;
        }

        int8_t enrollResult = getFingerprintEnroll(adminTaskId);
        if (enrollResult == FINGERPRINT_OK) {
          Serial.println("✅ ENROLLMENT SUCCESS: Slot " + String(adminTaskId));
        } else {
          Serial.println("❌ Enrollment failed with error: " + String(enrollResult));
        }
      }
      break;

    case DELETE:
      {
        if (adminTaskId > maxCapacity) {
          Serial.println("❌ ERROR: Slot " + String(adminTaskId) + " exceeds sensor capacity");
          break;
        }

        // ✅ ENHANCED DELETE CONFIRMATION
        updateDisplay("Confirm Delete", "Slot: " + String(adminTaskId));
        delay(1000);

        // Show what roll ID this affects
        int roll_id = floor((adminTaskId - 1) / 2) + 1;
        updateDisplay("Affects Roll", "#" + String(roll_id));
        delay(1000);

        updateDisplay("Deleting...", "Please wait");

        if (finger.deleteModel(adminTaskId) == FINGERPRINT_OK) {
          updateDisplay("Deleted!", "Slot: " + String(adminTaskId));
          delay(1000);
          updateDisplay("Roll #" + String(roll_id), "is now available");
          Serial.println("✅ SUCCESS: Template deleted from slot " + String(adminTaskId));
          Serial.println("📋 Roll ID " + String(roll_id) + " is now available for re-enrollment");
        } else {
          updateDisplay("Delete Failed", "Slot: " + String(adminTaskId));
          Serial.println("❌ ERROR: Could not delete template from slot " + String(adminTaskId));
        }
        delay(2000);
      }
      break;

    case DELETE_ALL:
      {
        if (finger.emptyDatabase() == FINGERPRINT_OK) {
          updateDisplay("All Deleted!", "Memory cleared");
          Serial.println("✅ SUCCESS: All fingerprints deleted!");
        } else {
          updateDisplay("Delete Failed", "Error occurred");
          Serial.println("❌ ERROR: Failed to delete all fingerprints");
        }
        delay(2000);
      }
      break;

    case MATCH:
      {
        Serial.println("=== FINGERPRINT MATCHING TEST ===");
        updateDisplay("Scan Test", "Place finger...");

        int found_id = getFingerprintID();
        if (found_id > 0) {
          updateDisplay("Match Found", "Slot #" + String(found_id));
          Serial.println("✅ SUCCESS: Match found for slot #" + String(found_id));

          // Convert to roll ID
          int class_roll_id = floor((found_id - 1) / 2) + 1;
          Serial.println("--> Corresponding Roll ID: #" + String(class_roll_id));
        } else if (found_id == -1) {
          updateDisplay("No Match", "In database");
          Serial.println("ℹ️  No match found in database");
        } else {
          updateDisplay("Scan Error", "Try again");
          Serial.println("❌ ERROR: Scanning failed");
        }
        delay(3000);
      }
      break;
  }

  // Reset admin task
  currentAdminTask = NONE;
  adminTaskId = 0;
  Serial.println("\n--- Admin Task Complete. Returning to normal operation. ---");
}




// =============================================
// SETUP FUNCTION (RUNS ONCE AT BOOT)
// =============================================


void setup() {
  Serial.begin(115200);  // Initialize serial communication for debugging
  delay(500);            // Give serial port time to initialize
  Serial.println("\n\n--- A.R.I.S.E. Firmware v3.2 Booting ---");


  // Load ALL configuration
  loadAllConfig();

  //Show loaded configuration
  Serial.println("Loaded Configuration:");
  Serial.println(" WiFi: " + String(ssid));
  Serial.println(" Server: " + String(server_ip));

  // Initialize OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("CRITICAL FAILURE: SSD1306 allocation failed.");
    for (;;)
      ;  // Halt execution if display fails
  }

  updateDisplay("Calibrating", "ADC...");

  // Initialize ADC calibration FIRST
  setupADCCalibration();

  collectCalibrationData();
  batteryPercentageCalibration();

  // Check battery safety immediately on boot
  updateBatteryPercentage();
  checkBatterySafety();  // Will shutdown/sleep if battery critical

  updateDisplay("Booting...");
  Serial.println("[OK] OLED Display Initialized.");
  delay(1000);



  // Initialize Universal Fingerprint Sensor
  Serial.println("\n--- Initializing Universal Fingerprint Sensor ---");
  updateDisplay("Init Sensor...");
  sensorSerial.begin(57600, SERIAL_8N1, 16, 17);
  delay(2000);

  // Clear any garbage data from serial buffer
  while (sensorSerial.available()) {
    sensorSerial.read();
  }

  // Try initialization multiple times
  bool sensorInitialized = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (finger.begin()) {
      sensorInitialized = true;

      // Force AS608 mode if detection is wrong
      if (finger.getSensorName().indexOf("R307") != -1) {
        Serial.println("⚠️  Wrong detection! Forcing AS608 mode...");
        // We'll handle this in the main logic
      }

      break;
    }
    Serial.println("Retrying sensor initialization...");
    delay(1000);
  }

  if (sensorInitialized) {
    Serial.println("SUCCESS! " + finger.getSensorName() + " detected.");
    updateDisplay("Sensor OK!");

    // Run diagnostic check
    checkFingerprintDatabase();
    delay(1500);
  } else {
    Serial.println("FAILURE! Did not find fingerprint sensor.");
    updateDisplay("Sensor Error!");
    // Don't halt - continue in limited mode
    Serial.println("Continuing in limited mode without fingerprint sensor.");
  }



  // Connect to WiFi Network
  updateDisplay("Connecting...");
  Serial.println("\n--- Attempting to Connect to WiFi ---");
  WiFi.begin(ssid, password);




  // Wait for WiFi connection with timeout
  int wifi_timeout_counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(". ");
    wifi_timeout_counter++;

    // Continuously check battery during WiFi connection
    updateBatteryPercentage();
    checkBatterySafety();

    updateDisplay("Connecting...");
    if (wifi_timeout_counter >= 40) {  // 20 second timeout (40 * 500ms)
      Serial.println("\nFAILURE: WiFi Connection Timed Out!");
      break;
    }
  }




  // Handle WiFi connection result
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nSUCCESS! WiFi Connected.");
    updateDisplay("Connected!");
    delay(1500);
  } else {
    Serial.println("\nWARNING: Proceeding in offline mode.");
    updateDisplay("WiFi Error!");
    delay(2000);
  }


  // SERIALPORT MESSAGES
  Serial.println("\n--- Setup Complete. Awaiting Session. ---");
  Serial.println("--- A.R.I.S.E. Admin Interface ---");
  Serial.println("\n📋 AVAILABLE COMMANDS:");
  Serial.println("\n--- WiFi & Server Setup ---");
  Serial.println("• wifi YourNetworkName YourPassword");
  Serial.println("• server http://your-server-ip:5000");
  Serial.println("• config                    (show current settings)");
  Serial.println("• reset-config              (reset to factory defaults)");

  Serial.println("\n--- Fingerprint Management ---");
  Serial.println("• enroll <ID>               (save fingerprint in slot 1-" + String(finger.getMaxCapacity()) + ")");
  Serial.println("• delete <ID>               (remove fingerprint from slot 1-" + String(finger.getMaxCapacity()) + ")");
  Serial.println("• delete-all                (delete ALL fingerprints)");
  Serial.println("• match                     (test fingerprint scanning)");
  Serial.println("• sensor-info               (show sensor details)");
  Serial.println("• help                      (show command help)");

  Serial.println("\n--- Examples ---");
  Serial.println("wifi SchoolWiFi MyPassword123");
  Serial.println("server http://192.168.1.100:5000");
  Serial.println("enroll 10");
  Serial.println("sensor-info");
}




// =============================================
// MAIN LOOP FUNCTION (RUNS CONTINUOUSLY)
// =============================================


void loop() {
  unsigned long currentTime = millis();

  // ✅ CHANGED: NON-BLOCKING battery check
  updateBatteryNonBlocking();

  // ✅ HIGH PRIORITY: Serial commands
  handleSerialCommands();

  // Network tasks only when WiFi connected
  if (WiFi.status() == WL_CONNECTED) {
    // Check server session status periodically
    if (currentTime - lastStatusCheck > statusCheckInterval) {
      checkServerSessionStatus();
      lastStatusCheck = currentTime;
    }
   
    // Send heartbeat to server periodically
    if (currentTime - lastHeartbeat > heartbeatInterval) {
      sendHeartbeat();
      lastHeartbeat = currentTime;
    }
   
    // Sync offline queue if items pending
    if (queueCount > 0) {
      tryToSyncQueue();
    }
  }

  // --- MAIN STATE MACHINE LOGIC ---
  if (currentAdminTask != NONE) {
    // STATE: ADMIN TASK MODE
    executeAdminTask();
  } else if (isSessionActive) {
    // STATE: ATTENDANCE MODE - Active session, ready for fingerprints
    // ✅ FIXED: Only show "Place Finger" once, let getFingerprintID handle the rest
    int sensor_id = getFingerprintID();
    if (sensor_id > 0) {
      // Convert sensor slot ID to class roll ID (2 fingerprints per student)
      int class_roll_id = floor((sensor_id - 1) / 2) + 1;
      updateDisplay("Identified", "Roll #" + String(class_roll_id));
      delay(500);
      sendToServer(class_roll_id);
    }
    // ✅ FIXED: If no finger detected, just continue silently
    // The display will remain "Place Finger" until next scan
  } else {
    // STATE: AWAITING SESSION (IDLE)
    updateDisplay("Ready For", "Session...");
    delay(200);
  }
}
