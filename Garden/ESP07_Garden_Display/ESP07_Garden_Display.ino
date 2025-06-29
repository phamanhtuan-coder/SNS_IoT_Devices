#define DEVICE_ID "ESP07_GARDEN_001"
#define FIRMWARE_VERSION "1.0.0"

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LiquidCrystal_I2C.h>

// ===== ESP07 GARDEN DISPLAY CONTROLLER =====
// Nhận data từ ESP Master Garden qua ESP-NOW
// Hiển thị trên OLED (thời gian) và LCD (thông số xoay vòng)
// RGB LED thể hiện status

// ===== DISPLAY CONFIGURATION =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

// ===== RGB LED PINS =====
#define RGB_RED_PIN 12
#define RGB_GREEN_PIN 13
#define RGB_BLUE_PIN 14

// ===== DISPLAY OBJECTS =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// ===== ESP MASTER GARDEN MAC =====
uint8_t masterGardenMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA7}; // Thay bằng MAC thực

// ===== ESP-NOW MESSAGE STRUCTURE =====
struct GardenMessage {
  char messageType[16];    // "sensor_data", "command", "status"
  char deviceSerial[32];   // ESP07 serial
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  char currentTime[16];
  char command[32];        // pump_on, pump_off, etc.
  bool success;
  unsigned long timestamp;
};

static GardenMessage receiveBuffer;
static GardenMessage sendBuffer;

// ===== GARDEN DATA STORAGE =====
struct GardenData {
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  String currentTime;
  String status;
  unsigned long lastUpdated;
  bool dataValid;
};

GardenData gardenData;

// ===== DISPLAY STATE =====
int currentLCDPage = 0;
const int totalLCDPages = 4;
unsigned long lastLCDUpdate = 0;
const unsigned long lcdUpdateInterval = 3000; // 3 seconds per page

// ===== CONNECTION STATUS =====
bool masterConnected = false;
unsigned long lastMasterMessage = 0;

// ===== STATISTICS =====
unsigned long messagesReceived = 0;
unsigned long displayUpdates = 0;
unsigned long systemUptime = 0;

// ===== RGB STATUS COLORS =====
enum GardenStatus {
  STATUS_GOOD = 0,     // Green - All good
  STATUS_WATERING = 1, // Blue - Watering in progress
  STATUS_DRY = 2,      // Yellow - Low soil moisture
  STATUS_ERROR = 3,    // Red - Error/Alert
  STATUS_RAIN = 4,     // Purple - Rain detected
  STATUS_NIGHT = 5     // White - Night mode
};

// ✅ SAFE STRING COPY HELPER
void safeStringCopy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  Serial.println("\n=== ESP07 GARDEN DISPLAY v1.0.0 ===");
  Serial.println("Device: " + String(DEVICE_ID));
  Serial.println("My MAC: " + WiFi.macAddress());
  
  // Initialize I2C
  Wire.begin(4, 5);  // SDA=GPIO4, SCL=GPIO5
  Serial.println("[I2C] ✓ Initialized (SDA=4, SCL=5)");
  
  // Initialize displays
  initializeDisplays();
  
  // Initialize RGB LED
  initializeRGB();
  
  // Initialize garden data
  initializeGardenData();
  
  // Setup ESP-NOW
  setupESPNow();
  
  // Show startup screen
  showStartupScreen();
  
  Serial.println("[INIT] ✓ ESP07 Garden Display Ready");
  systemUptime = millis();
}

void initializeDisplays() {
  Serial.println("[DISPLAY] Initializing displays...");
  
  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[OLED] ✗ SSD1306 allocation failed");
  } else {
    Serial.println("[OLED] ✓ SSD1306 128x64 initialized");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
  }
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  Serial.println("[LCD] ✓ 16x2 I2C initialized");
  
  Serial.println("[DISPLAY] ✓ All displays ready");
}

void initializeRGB() {
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  
  // Test RGB
  setRGBColor(255, 0, 0);   // Red
  delay(300);
  setRGBColor(0, 255, 0);   // Green
  delay(300);
  setRGBColor(0, 0, 255);   // Blue
  delay(300);
  setRGBColor(0, 0, 0);     // Off
  
  Serial.println("[RGB] ✓ RGB LED initialized");
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
  gardenData.lastUpdated = 0;
  gardenData.dataValid = false;
  
  Serial.println("[DATA] ✓ Garden data structure initialized");
}

void setupESPNow() {
  Serial.println("[ESP-NOW] Initializing...");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(1);
  wifi_set_sleep_type(NONE_SLEEP_T);
  
  int initResult = esp_now_init();
  if (initResult != 0) {
    Serial.println("[ESP-NOW] Init FAILED: " + String(initResult));
    delay(5000);
    ESP.restart();
  }
  
  Serial.println("[ESP-NOW] Init OK");
  
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  // Add ESP Master Garden as peer
  Serial.print("[ESP-NOW] Adding Master Garden peer: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", masterGardenMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  int result = esp_now_add_peer(masterGardenMAC, ESP_NOW_ROLE_CONTROLLER, 1, NULL, 0);
  if (result != 0) {
    result = esp_now_add_peer(masterGardenMAC, ESP_NOW_ROLE_CONTROLLER, 0, NULL, 0);
  }
  
  if (result == 0) {
    Serial.println("[ESP-NOW] ✓ Master peer added");
  } else {
    Serial.println("[ESP-NOW] ✗ Peer add failed: " + String(result));
  }
  
  Serial.println("[ESP-NOW] Setup complete");
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    Serial.println("[ESP-NOW] ✓ Response sent to Master");
  } else {
    Serial.println("[ESP-NOW] ✗ Response send failed: " + String(sendStatus));
  }
}

void onDataReceived(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  messagesReceived++;
  
  if (memcmp(mac_addr, masterGardenMAC, 6) != 0 || len != sizeof(GardenMessage)) {
    Serial.println("[ESP-NOW] RX #" + String(messagesReceived) + " ✗ Invalid");
    return;
  }
  
  memcpy(&receiveBuffer, incomingData, len);
  
  masterConnected = true;
  lastMasterMessage = millis();
  
  String msgType = String(receiveBuffer.messageType);
  Serial.println("[ESP-NOW] RX #" + String(messagesReceived) + ": " + msgType);
  
  if (msgType == "sensor_data") {
    updateGardenData();
    updateDisplays();
    updateRGBStatus();
    
  } else if (msgType == "command") {
    handleCommand();
    
  } else if (msgType == "heartbeat") {
    sendHeartbeatResponse();
  }
}

void updateGardenData() {
  gardenData.temperature = receiveBuffer.temperature;
  gardenData.humidity = receiveBuffer.humidity;
  gardenData.soilMoisture = receiveBuffer.soilMoisture;
  gardenData.lightLevel = receiveBuffer.lightLevel;
  gardenData.rainDetected = receiveBuffer.rainDetected;
  gardenData.pumpRunning = receiveBuffer.pumpRunning;
  gardenData.currentTime = String(receiveBuffer.currentTime);
  gardenData.lastUpdated = millis();
  gardenData.dataValid = true;
  
  // Determine status
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
  
  Serial.println("[DATA] Updated: " + String(gardenData.temperature) + "°C, " +
                 String(gardenData.humidity) + "%, " +
                 "Soil:" + String(gardenData.soilMoisture) + "%, " +
                 "Status:" + gardenData.status);
}

void handleCommand() {
  String cmd = String(receiveBuffer.command);
  Serial.println("[CMD] Received: " + cmd);
  
  if (cmd == "pump_on") {
    gardenData.pumpRunning = true;
    gardenData.status = "Watering...";
  } else if (cmd == "pump_off") {
    gardenData.pumpRunning = false;
    gardenData.status = "Pump Stopped";
  }
  
  updateDisplays();
  updateRGBStatus();
  
  // Send command response
  sendCommandResponse(true);
}

void sendHeartbeatResponse() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "status", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.deviceSerial, DEVICE_ID, sizeof(sendBuffer.deviceSerial));
  safeStringCopy(sendBuffer.command, "heartbeat_ack", sizeof(sendBuffer.command));
  
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  delay(10);
  int result = esp_now_send(masterGardenMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[HEARTBEAT] ✓ Response sent");
  } else {
    Serial.println("[HEARTBEAT] ✗ Response failed: " + String(result));
  }
}

void sendCommandResponse(bool success) {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "status", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.deviceSerial, DEVICE_ID, sizeof(sendBuffer.deviceSerial));
  safeStringCopy(sendBuffer.command, "command_ack", sizeof(sendBuffer.command));
  
  sendBuffer.success = success;
  sendBuffer.timestamp = millis();
  
  delay(10);
  int result = esp_now_send(masterGardenMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[CMD] ✓ Response sent");
  } else {
    Serial.println("[CMD] ✗ Response failed: " + String(result));
  }
}

void updateDisplays() {
  updateOLED();
  updateLCD();
  displayUpdates++;
}

void updateOLED() {
  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GARDEN MONITOR");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  
  // Time (large)
  display.setTextSize(2);
  display.setCursor(20, 15);
  display.println(gardenData.currentTime);
  
  // Status
  display.setTextSize(1);
  display.setCursor(0, 35);
  display.println("Status: " + gardenData.status);
  
  // Connection indicator
  display.setCursor(0, 45);
  if (masterConnected) {
    display.println("Connected");
  } else {
    display.println("Disconnected");
  }
  
  // Data age
  if (gardenData.dataValid) {
    unsigned long dataAge = (millis() - gardenData.lastUpdated) / 1000;
    display.setCursor(0, 55);
    display.println("Updated: " + String(dataAge) + "s ago");
  }
  
  display.display();
}

void updateLCD() {
  // Check if it's time to change page
  if (millis() - lastLCDUpdate > lcdUpdateInterval) {
    currentLCDPage = (currentLCDPage + 1) % totalLCDPages;
    lastLCDUpdate = millis();
  }
  
  lcd.clear();
  
  switch(currentLCDPage) {
    case 0: // Temperature & Humidity
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(gardenData.temperature, 1);
      lcd.print("C");
      
      lcd.setCursor(0, 1);
      lcd.print("Humidity: ");
      lcd.print(gardenData.humidity, 0);
      lcd.print("%");
      break;
      
    case 1: // Soil & Light
      lcd.setCursor(0, 0);
      lcd.print("Soil: ");
      lcd.print(gardenData.soilMoisture);
      lcd.print("%");
      
      lcd.setCursor(0, 1);
      lcd.print("Light: ");
      lcd.print(gardenData.lightLevel);
      lcd.print(" lux");
      break;
      
    case 2: // Rain & Pump
      lcd.setCursor(0, 0);
      lcd.print("Rain: ");
      lcd.print(gardenData.rainDetected ? "YES" : "NO");
      
      lcd.setCursor(0, 1);
      lcd.print("Pump: ");
      lcd.print(gardenData.pumpRunning ? "RUNNING" : "STOPPED");
      break;
      
    case 3: // Status & Time
      lcd.setCursor(0, 0);
      lcd.print(gardenData.status);
      
      lcd.setCursor(0, 1);
      lcd.print("Time: ");
      lcd.print(gardenData.currentTime);
      break;
  }
}

void updateRGBStatus() {
  GardenStatus status = determineGardenStatus();
  
  switch(status) {
    case STATUS_GOOD:
      setRGBColor(0, 255, 0);    // Green
      break;
    case STATUS_WATERING:
      setRGBColor(0, 0, 255);    // Blue
      break;
    case STATUS_DRY:
      setRGBColor(255, 255, 0);  // Yellow
      break;
    case STATUS_ERROR:
      setRGBColor(255, 0, 0);    // Red
      break;
    case STATUS_RAIN:
      setRGBColor(128, 0, 128);  // Purple
      break;
    case STATUS_NIGHT:
      setRGBColor(255, 255, 255); // White (dim)
      break;
  }
}

GardenStatus determineGardenStatus() {
  if (!gardenData.dataValid) {
    return STATUS_ERROR;
  }
  
  if (gardenData.rainDetected) {
    return STATUS_RAIN;
  }
  
  if (gardenData.pumpRunning) {
    return STATUS_WATERING;
  }
  
  if (gardenData.soilMoisture < 30) {
    return STATUS_DRY;
  }
  
  if (gardenData.lightLevel < 200) {
    return STATUS_NIGHT;
  }
  
  if (gardenData.temperature > 35) {
    return STATUS_ERROR;  // Too hot
  }
  
  return STATUS_GOOD;
}

void setRGBColor(int red, int green, int blue) {
  // ESP8266 PWM range is 0-1023
  analogWrite(RGB_RED_PIN, map(red, 0, 255, 0, 1023));
  analogWrite(RGB_GREEN_PIN, map(green, 0, 255, 0, 1023));
  analogWrite(RGB_BLUE_PIN, map(blue, 0, 255, 0, 1023));
}

void showStartupScreen() {
  // OLED startup
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ESP07 GARDEN");
  display.println("Starting...");
  display.println("");
  display.println("Version: " + String(FIRMWARE_VERSION));
  display.println("Device: " + String(DEVICE_ID));
  display.display();
  
  // LCD startup
  lcd.setCursor(0, 0);
  lcd.print("Garden Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  delay(2000);
}

void checkConnectionHealth() {
  if (masterConnected && (millis() - lastMasterMessage > 180000)) {
    masterConnected = false;
    Serial.println("[HEALTH] ✗ Master Garden timeout");
    
    // Update displays to show disconnected status
    gardenData.dataValid = false;
    gardenData.status = "Disconnected";
    updateDisplays();
    setRGBColor(255, 0, 0);  // Red for error
  }
}

void printStatus() {
  Serial.println("\n=== ESP07 GARDEN STATUS ===");
  Serial.println("Device: " + String(DEVICE_ID));
  Serial.println("Uptime: " + String((millis() - systemUptime) / 1000) + "s");
  Serial.println("Master: " + String(masterConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Messages RX: " + String(messagesReceived));
  Serial.println("Display Updates: " + String(displayUpdates));
  Serial.println("Current Page: " + String(currentLCDPage));
  
  if (gardenData.dataValid) {
    Serial.println("\n--- Garden Data ---");
    Serial.println("Temperature: " + String(gardenData.temperature) + "°C");
    Serial.println("Humidity: " + String(gardenData.humidity) + "%");
    Serial.println("Soil: " + String(gardenData.soilMoisture) + "%");
    Serial.println("Light: " + String(gardenData.lightLevel) + " lux");
    Serial.println("Rain: " + String(gardenData.rainDetected ? "YES" : "NO"));
    Serial.println("Pump: " + String(gardenData.pumpRunning ? "ON" : "OFF"));
    Serial.println("Status: " + gardenData.status);
    Serial.println("Time: " + gardenData.currentTime);
  }
  
  Serial.println("\nFree Heap: " + String(ESP.getFreeHeap()));
  Serial.println("=========================\n");
}

void loop() {
  // ===== CONNECTION HEALTH CHECK =====
  static unsigned long lastHealthCheck = 0;
  if (millis() - lastHealthCheck > 30000) {  // Every 30 seconds
    checkConnectionHealth();
    lastHealthCheck = millis();
  }
  
  // ===== STATUS PRINT =====
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 120000) {  // Every 2 minutes
    printStatus();
    lastStatusPrint = millis();
  }
  
  // ===== LCD PAGE ROTATION =====
  if (gardenData.dataValid && (millis() - lastLCDUpdate > lcdUpdateInterval)) {
    updateLCD();
  }
  
  // ===== DEMO MODE (if no data) =====
  static unsigned long lastDemo = 0;
  if (!gardenData.dataValid && (millis() - lastDemo > 5000)) {
    showDemoData();
    lastDemo = millis();
  }
  
  yield();
  delay(100);
}

void showDemoData() {
  // Show demo data when no real data available
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DEMO MODE");
  lcd.setCursor(0, 1);
  lcd.print("Waiting for data");
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GARDEN MONITOR");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Waiting for");
  display.println("Master connection...");
  display.setCursor(0, 50);
  display.println("Status: Standby");
  display.display();
  
  // Cycle RGB colors slowly
  static int demoColor = 0;
  switch(demoColor % 3) {
    case 0: setRGBColor(50, 0, 0); break;   // Dim red
    case 1: setRGBColor(0, 50, 0); break;   // Dim green  
    case 2: setRGBColor(0, 0, 50); break;   // Dim blue
  }
  demoColor++;
}