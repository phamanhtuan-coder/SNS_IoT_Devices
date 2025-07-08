#define FIRMWARE_VERSION "2.2.0"
#define DEVICE_ID "ESP8266_GARDEN_HUB_001"
#define GARDEN_SERIAL "ESP8266_GARDEN_001"

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ===== ESP8266 GARDEN HUB - OLED + LCD VERSION =====
// Dual display system: OLED for detailed info, LCD for key status
// Both displays share same I2C bus with different addresses

// ===== ESP8266 CHIP PIN MAPPING =====
// ESP8266 chip pins: GPIO0, GPIO2, GPIO12, GPIO13, GPIO14, GPIO15, GPIO16
// TXD, RXD: Serial communication with Mega Hub
// GPIO2: SDA (I2C) - Shared bus for OLED + LCD
// GPIO14: SCL (I2C) - Shared bus for OLED + LCD
// 
// ===== AVAILABLE PINS FOR EXPANSION =====
// GPIO12 - Available ✅
// GPIO13 - Available ✅  
// GPIO16 - Available ✅

// ===== I2C DEVICE ADDRESSES =====
#define OLED_ADDRESS 0x3C
#define LCD_ADDRESS 0x27    // Common address, could be 0x3F

// ===== DISPLAY CONFIGURATION =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define LCD_COLUMNS 16
#define LCD_ROWS 2

// ===== DISPLAY OBJECTS =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// ===== GARDEN DATA STRUCTURE =====
struct GardenData {
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  String currentTime;
  String status;
  String rgbStatus;
  unsigned long lastUpdated;
  bool dataValid;
};

GardenData gardenData;

// ===== DISPLAY STATE =====
int currentDisplayPage = 0;
const int totalDisplayPages = 4;
unsigned long lastPageUpdate = 0;
const unsigned long pageUpdateInterval = 3000; // 3 seconds per page

// ===== LCD DISPLAY STATE =====
int currentLCDPage = 0;
const int totalLCDPages = 3;
unsigned long lastLCDUpdate = 0;
const unsigned long lcdUpdateInterval = 2000; // 2 seconds per LCD page

// ===== CONNECTION STATUS =====
bool megaConnected = false;
unsigned long lastMegaMessage = 0;

// ===== STATISTICS =====
unsigned long dataReceived = 0;
unsigned long displayUpdates = 0;
unsigned long commandsProcessed = 0;
unsigned long systemUptime = 0;

// ===== FUNCTION PROTOTYPES =====
void initializeDisplays();
void initializeGardenData();
void handleMegaMessage(String message);
void handleGardenData(String dataMessage);
void handlePumpStatus(String statusMessage);
void handleGardenCommand(String cmdMessage);
void updateGardenStatus();
void updateDisplays();
void updateOLED();
void updateLCD();
void sendGardenCommandResponse(String command, bool success, String result);
void sendGardenStatusResponse();
void showStartupScreen();
void showDemoData();
void checkConnectionHealth();
void printStatus();
void scanI2CDevices();

void setup() {
  Serial.begin(115200);  // Communication with Arduino Mega
  delay(2000);
  
  Serial.println("\n=== ESP8266 GARDEN HUB v2.2.0 (OLED + LCD) ===");
  Serial.println("Device: " + String(DEVICE_ID));
  Serial.println("Dual Display: OLED + LCD on shared I2C bus");
  
  // Initialize I2C
  Wire.begin(2, 14);  // SDA=GPIO2, SCL=GPIO14
  Serial.println("[I2C] ✓ Initialized on GPIO2(SDA) and GPIO14(SCL)");
  
  // Scan for I2C devices
  scanI2CDevices();
  
  // Initialize displays
  initializeDisplays();
  
  // Initialize garden data
  initializeGardenData();
  
  // Show startup screen
  showStartupScreen();
  
  Serial.println("[INIT] ✓ Garden Hub Ready (OLED + LCD)");
  systemUptime = millis();
}

void initializeDisplays() {
  Serial.println("[DISPLAY] Initializing dual displays...");
  
  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[OLED] ✗ Failed at address 0x" + String(OLED_ADDRESS, HEX));
  } else {
    Serial.println("[OLED] ✓ Ready at address 0x" + String(OLED_ADDRESS, HEX));
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
  }
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  
  // Test LCD
  lcd.setCursor(0, 0);
  lcd.print("LCD Test...");
  delay(1000);
  lcd.clear();
  
  Serial.println("[LCD] ✓ Ready at address 0x" + String(LCD_ADDRESS, HEX));
  Serial.println("[DISPLAY] ✓ Both displays initialized successfully");
}

void scanI2CDevices() {
  Serial.println("[I2C] Scanning for devices...");
  
  int deviceCount = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.println("[I2C] Device found at address 0x" + String(address, HEX));
      deviceCount++;
    }
  }
  
  if (deviceCount == 0) {
    Serial.println("[I2C] ✗ No devices found!");
  } else {
    Serial.println("[I2C] ✓ Found " + String(deviceCount) + " device(s)");
  }
}

void initializeGardenData() {
  gardenData.temperature = 0;
  gardenData.humidity = 0;
  gardenData.soilMoisture = 0;
  gardenData.lightLevel = 0;
  gardenData.rainDetected = false;
  gardenData.pumpRunning = false;
  gardenData.currentTime = "00:00:00";
  gardenData.status = "Waiting...";
  gardenData.rgbStatus = "Unknown";
  gardenData.lastUpdated = 0;
  gardenData.dataValid = false;
  
  Serial.println("[DATA] ✓ Structure initialized");
}

void loop() {
  // Handle Mega communication
  if (Serial.available()) {
    String megaMessage = Serial.readStringUntil('\n');
    handleMegaMessage(megaMessage);
  }
  
  // Periodic tasks
  static unsigned long lastHealthCheck = 0;
  if (millis() - lastHealthCheck > 30000) {
    checkConnectionHealth();
    lastHealthCheck = millis();
  }
  
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 120000) {
    printStatus();
    lastStatusPrint = millis();
  }
  
  // OLED page rotation
  if (gardenData.dataValid && (millis() - lastPageUpdate > pageUpdateInterval)) {
    currentDisplayPage = (currentDisplayPage + 1) % totalDisplayPages;
    updateOLED();
    lastPageUpdate = millis();
  }
  
  // LCD page rotation
  if (gardenData.dataValid && (millis() - lastLCDUpdate > lcdUpdateInterval)) {
    currentLCDPage = (currentLCDPage + 1) % totalLCDPages;
    updateLCD();
    lastLCDUpdate = millis();
  }
  
  // Demo mode when no data
  static unsigned long lastDemo = 0;
  if (!gardenData.dataValid && (millis() - lastDemo > 5000)) {
    showDemoData();
    lastDemo = millis();
  }
  
  yield();
  delay(10);
}

void handleMegaMessage(String message) {
  message.trim();
  if (message.length() == 0) return;
  
  megaConnected = true;
  lastMegaMessage = millis();
  
  Serial.println("[MEGA→HUB] " + message);
  
  if (message.startsWith("GARDEN_DATA:")) {
    handleGardenData(message);
  } else if (message.startsWith("PUMP_STATUS:")) {
    handlePumpStatus(message);
  } else if (message.startsWith("RGB_STATUS:")) {
    String rgbData = message.substring(11);
    gardenData.rgbStatus = rgbData;
    Serial.println("[RGB] Status from Mega: " + rgbData);
    updateDisplays(); // Update both displays when RGB status changes
  } else if (message.startsWith("CMD:")) {
    handleGardenCommand(message);
  } else {
    Serial.println("[MEGA] Unknown: " + message);
  }
}

void handleGardenData(String dataMessage) {
  String data = dataMessage.substring(12);
  
  // Split by comma
  int values_count = 0;
  String values[7];
  int startIndex = 0;
  
  for (int i = 0; i <= data.length() && values_count < 7; i++) {
    if (i == data.length() || data.charAt(i) == ',') {
      values[values_count] = data.substring(startIndex, i);
      startIndex = i + 1;
      values_count++;
    }
  }
  
  if (values_count >= 6) {
    // Update garden data
    gardenData.temperature = values[0].toFloat();
    gardenData.humidity = values[1].toFloat();
    gardenData.soilMoisture = values[2].toInt();
    gardenData.lightLevel = values[3].toInt();
    gardenData.rainDetected = (values[4].toInt() == 1);
    gardenData.pumpRunning = (values[5].toInt() == 1);
    gardenData.currentTime = values[6];
    gardenData.lastUpdated = millis();
    gardenData.dataValid = true;
    
    // Update status
    updateGardenStatus();
    
    // Update both displays
    updateDisplays();
    
    dataReceived++;
    
    Serial.println("[DATA] ✓ " + String(gardenData.temperature) + "°C, " +
                   String(gardenData.humidity) + "%, Soil:" + 
                   String(gardenData.soilMoisture) + "%, " + gardenData.status);
    
    // Send status response to Mega
    sendGardenStatusResponse();
  } else {
    Serial.println("[DATA] ✗ Invalid format");
  }
}

void handlePumpStatus(String statusMessage) {
  String status = statusMessage.substring(12);
  gardenData.pumpRunning = (status.toInt() == 1);
  
  updateGardenStatus();
  updateDisplays();
  
  Serial.println("[PUMP] Status: " + String(gardenData.pumpRunning ? "ON" : "OFF"));
}

void updateGardenStatus() {
  if (gardenData.rainDetected) {
    gardenData.status = "Rain Detected";
  } else if (gardenData.pumpRunning) {
    gardenData.status = "Watering...";
  } else if (gardenData.soilMoisture < 30) {
    gardenData.status = "Soil Dry";
  } else if (gardenData.lightLevel < 200) {
    gardenData.status = "Night Mode";
  } else {
    gardenData.status = "All Good";
  }
}

void updateDisplays() {
  updateOLED();
  updateLCD();
  displayUpdates++;
}

void updateOLED() {
  display.clearDisplay();
  
  // Rotate between different pages
  switch(currentDisplayPage % totalDisplayPages) {
    case 0: // Main status
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("GARDEN MONITOR");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      
      display.setTextSize(2);
      display.setCursor(20, 15);
      display.println(gardenData.currentTime);
      
      display.setTextSize(1);
      display.setCursor(0, 35);
      display.println("Status: " + gardenData.status);
      
      display.setCursor(0, 45);
      display.println(megaConnected ? "Connected" : "Disconnected");
      
      display.setCursor(0, 55);
      display.println("RGB: " + gardenData.rgbStatus);
      break;
      
    case 1: // Temperature & Humidity
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("TEMPERATURE & HUMIDITY");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      
      display.setTextSize(2);
      display.setCursor(0, 20);
      display.println(String(gardenData.temperature, 1) + "C");
      
      display.setCursor(0, 40);
      display.println(String(gardenData.humidity, 0) + "%");
      
      display.setTextSize(1);
      display.setCursor(0, 55);
      if (gardenData.dataValid) {
        unsigned long dataAge = (millis() - gardenData.lastUpdated) / 1000;
        display.println("Updated: " + String(dataAge) + "s ago");
      }
      break;
      
    case 2: // Soil & Light
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("SOIL & LIGHT");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      
      display.setTextSize(2);
      display.setCursor(0, 20);
      display.println("Soil: " + String(gardenData.soilMoisture) + "%");
      
      display.setTextSize(1);
      display.setCursor(0, 40);
      display.println("Light: " + String(gardenData.lightLevel) + " lux");
      
      display.setCursor(0, 50);
      display.println("Rain: " + String(gardenData.rainDetected ? "YES" : "NO"));
      
      display.setCursor(0, 55);
      display.println("RGB: " + gardenData.rgbStatus);
      break;
      
    case 3: // System Status
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("SYSTEM STATUS");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      
      display.setCursor(0, 20);
      display.println("Pump: " + String(gardenData.pumpRunning ? "RUNNING" : "STOPPED"));
      
      display.setCursor(0, 30);
      display.println("Uptime: " + String((millis() - systemUptime) / 60000) + "min");
      
      display.setCursor(0, 40);
      display.println("Data RX: " + String(dataReceived));
      
      display.setCursor(0, 50);
      display.println("Display: " + String(displayUpdates));
      
      display.setCursor(0, 55);
      display.println("Page " + String(currentDisplayPage + 1) + "/" + String(totalDisplayPages));
      break;
  }
  
  display.display();
}

void updateLCD() {
  lcd.clear();
  
  // Rotate between different LCD pages
  switch(currentLCDPage % totalLCDPages) {
    case 0: // Temperature and Time
      lcd.setCursor(0, 0);
      lcd.print("Temp: " + String(gardenData.temperature, 1) + "C");
      lcd.setCursor(0, 1);
      lcd.print("Time: " + gardenData.currentTime);
      break;
      
    case 1: // Soil and Pump Status
      lcd.setCursor(0, 0);
      lcd.print("Soil: " + String(gardenData.soilMoisture) + "%");
      lcd.setCursor(0, 1);
      lcd.print("Pump: " + String(gardenData.pumpRunning ? "RUNNING" : "STOPPED"));
      break;
      
    case 2: // Status and RGB
      lcd.setCursor(0, 0);
      lcd.print(gardenData.status);
      lcd.setCursor(0, 1);
      lcd.print("RGB: " + gardenData.rgbStatus.substring(0, 10)); // Truncate for LCD
      break;
  }
}

void handleGardenCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4);
  
  if (!cmdData.startsWith("GDN:")) return;
  
  String action = cmdData.substring(4);
  commandsProcessed++;
  
  bool success = false;
  String result = "ERR";
  
  if (action == "PON") {
    Serial.println("GARDEN_CMD:PUMP_ON");
    success = true;
    result = "PON";
    
  } else if (action == "POF") {
    Serial.println("GARDEN_CMD:PUMP_OFF");
    success = true;
    result = "POF";
    
  } else if (action == "ATG") {
    Serial.println("GARDEN_CMD:AUTO_TOGGLE");
    success = true;
    result = "ATG";
    
  } else if (action == "STS") {
    success = true;
    result = "STS";
    
  } else if (action == "DSP") {
    currentDisplayPage = (currentDisplayPage + 1) % totalDisplayPages;
    currentLCDPage = (currentLCDPage + 1) % totalLCDPages;
    updateDisplays();
    success = true;
    result = "DSP";
    
  } else if (action == "RGB") {
    Serial.println("GARDEN_CMD:RGB_TEST");
    success = true;
    result = "RGB";
    
  } else if (action == "REF") {
    Serial.println("GARDEN_CMD:REQUEST_DATA");
    success = true;
    result = "REF";
  }
  
  sendGardenCommandResponse(action, success, result);
}

void sendGardenCommandResponse(String command, bool success, String result) {
  String response = "RESP:{";
  response += "\"s\":" + String(success ? "1" : "0") + ",";
  response += "\"r\":\"" + result + "\",";
  response += "\"d\":\"GDN\",";
  response += "\"c\":\"" + command + "\",";
  response += "\"oled_p\":" + String(currentDisplayPage) + ",";
  response += "\"lcd_p\":" + String(currentLCDPage) + ",";
  response += "\"t\":" + String(millis());
  response += "}";
  
  Serial.println(response);
}

void sendGardenStatusResponse() {
  String response = "RESP:{";
  response += "\"type\":\"garden_status\",";
  response += "\"deviceId\":\"" + String(GARDEN_SERIAL) + "\",";
  response += "\"oled_active\":true,";
  response += "\"lcd_active\":true,";
  response += "\"last_update\":" + String(gardenData.lastUpdated) + ",";
  response += "\"oled_page\":" + String(currentDisplayPage) + ",";
  response += "\"lcd_page\":" + String(currentLCDPage) + ",";
  response += "\"temperature\":" + String(gardenData.temperature) + ",";
  response += "\"humidity\":" + String(gardenData.humidity) + ",";
  response += "\"soil_moisture\":" + String(gardenData.soilMoisture) + ",";
  response += "\"pump_running\":" + String(gardenData.pumpRunning ? "true" : "false") + ",";
  response += "\"rgb_status\":\"" + gardenData.rgbStatus + "\",";
  response += "\"timestamp\":" + String(millis());
  response += "}";
  
  Serial.println(response);
}

void showStartupScreen() {
  // OLED startup
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ESP8266 GARDEN HUB");
  display.println("v" + String(FIRMWARE_VERSION));
  display.println("OLED + LCD Version");
  display.println("");
  display.println("Initializing...");
  display.println("Dual Display System");
  display.display();
  
  // LCD startup
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Garden Hub v2.2");
  lcd.setCursor(0, 1);
  lcd.print("Dual Display OK");
  
  delay(3000);
}

void showDemoData() {
  // OLED demo
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GARDEN MONITOR");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Waiting for");
  display.println("Mega connection...");
  display.setCursor(0, 40);
  display.println("Status: Standby");
  display.setCursor(0, 50);
  display.println("RGB: On Mega Hub");
  display.setCursor(0, 55);
  display.println("Dual Display Mode");
  display.display();
  
  // LCD demo
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waiting for");
  lcd.setCursor(0, 1);
  lcd.print("Mega Hub...");
}

void checkConnectionHealth() {
  if (megaConnected && (millis() - lastMegaMessage > 120000)) {
    megaConnected = false;
    gardenData.dataValid = false;
    gardenData.status = "Mega Timeout";
    gardenData.rgbStatus = "Offline";
    Serial.println("[HEALTH] ✗ Mega timeout");
    updateDisplays();
  }
}

void printStatus() {
  Serial.println("\n=== ESP8266 GARDEN HUB STATUS (OLED + LCD) ===");
  Serial.println("Device: " + String(DEVICE_ID));
  Serial.println("Version: " + String(FIRMWARE_VERSION));
  Serial.println("Uptime: " + String((millis() - systemUptime) / 1000) + "s");
  Serial.println("Mega: " + String(megaConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Data RX: " + String(dataReceived));
  Serial.println("Display Updates: " + String(displayUpdates));
  Serial.println("OLED Page: " + String(currentDisplayPage));
  Serial.println("LCD Page: " + String(currentLCDPage));
  
  if (gardenData.dataValid) {
    Serial.println("\n--- Garden Data ---");
    Serial.println("Temp: " + String(gardenData.temperature) + "°C");
    Serial.println("Humidity: " + String(gardenData.humidity) + "%");
    Serial.println("Soil: " + String(gardenData.soilMoisture) + "%");
    Serial.println("Light: " + String(gardenData.lightLevel) + " lux");
    Serial.println("Rain: " + String(gardenData.rainDetected ? "YES" : "NO"));
    Serial.println("Pump: " + String(gardenData.pumpRunning ? "ON" : "OFF"));
    Serial.println("Status: " + gardenData.status);
    Serial.println("RGB Status: " + gardenData.rgbStatus);
    Serial.println("Time: " + gardenData.currentTime);
    Serial.println("Age: " + String((millis() - gardenData.lastUpdated) / 1000) + "s");
  }
  
  Serial.println("\nFree Heap: " + String(ESP.getFreeHeap()));
  Serial.println("Chip: ESP8266 (bare chip)");
  Serial.println("I2C Devices: OLED(0x3C) + LCD(0x27)");
  Serial.println("I2C Bus: GPIO2(SDA) + GPIO14(SCL)");
  Serial.println("Available: GPIO12, GPIO13, GPIO16");
  Serial.println("============================================\n");
}