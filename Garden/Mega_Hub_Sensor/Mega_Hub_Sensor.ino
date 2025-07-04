#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>

#define FIRMWARE_VERSION "2.0.0"
#define HUB_ID "MEGA_HUB_GARDEN_001"

// ===== ARDUINO MEGA CENTRAL HUB WITH GARDEN SENSORS =====
// Serial0 (0,1): Debug Output / USB
// Serial1 (18,19): ESP8266 Socket Hub
// Serial2 (16,17): ESP8266 Master (doors)
// Serial3 (14,15): ESP8266 Master (garden)

// ===== GARDEN SENSOR PINS =====
#define DHT_PIN 22
#define DHT_TYPE DHT11
#define WATER_SENSOR_PIN A0
#define LIGHT_SENSOR_PIN 24         // Digital pin for LDR module
#define SOIL_MOISTURE_PIN A2
#define PUMP_RELAY_PIN 23

// ===== SENSOR OBJECTS =====
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;  // RTClib works with both DS3231 and DS1307 (MH RTC-2)

// ===== GARDEN SENSOR DATA =====
struct GardenData {
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  String currentTime;
  unsigned long lastUpdated;
};

GardenData gardenData;

// ===== DOOR DEVICE STATE MANAGEMENT =====
struct DeviceState {
  String serialNumber;
  String lastAction;
  bool doorOpen;
  int servoAngle;
  bool isOnline;
  unsigned long lastSeen;
  String status;
};

DeviceState devices[7] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", "", false, 0, false, 0, "offline"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", "", false, 0, false, 0, "offline"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", "", false, 0, false, 0, "offline"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", "", false, 0, false, 0, "offline"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", "", false, 0, false, 0, "offline"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", "", false, 0, false, 0, "offline"},
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", "", false, 0, false, 0, "offline"}
};

const int TOTAL_DEVICES = 7;

// ===== SYSTEM STATUS =====
bool socketHubConnected = false;
bool doorMasterConnected = false;
bool gardenMasterConnected = false;
unsigned long lastSocketMessage = 0;
unsigned long lastDoorMessage = 0;
unsigned long lastGardenMessage = 0;

// ===== AUTOMATION SETTINGS =====
int soilMoistureThreshold = 30;  // Percent - water when below this
int lightThreshold = 200;        // LUX - consider "dark" below this
bool autoWateringEnabled = true;
unsigned long pumpStartTime = 0;
const unsigned long maxPumpRunTime = 300000; // 5 minutes max

// ===== STATISTICS =====
unsigned long commandsProcessed = 0;
unsigned long responsesForwarded = 0;
unsigned long sensorReadings = 0;
unsigned long systemUptime = 0;

// ===== SAFE STRING COPY HELPER =====
void safeStringCopy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';  // Ensure null termination
  }
}

void setup() {
  // Initialize all serial ports
  Serial.begin(115200);   // Debug Output / USB
  Serial1.begin(115200);  // ESP8266 Socket Hub
  Serial2.begin(115200);  // ESP8266 Master (doors)
  Serial3.begin(115200);  // ESP8266 Master (garden)
  
  delay(2000);
  
  Serial.println("\n=== ARDUINO MEGA GARDEN HUB v2.0.0 ===");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Managing " + String(TOTAL_DEVICES) + " doors + Garden");
  Serial.println("Interfaces:");
  Serial.println("  Serial0: Debug Output / USB");
  Serial.println("  Serial1: ESP8266 Socket Hub");
  Serial.println("  Serial2: ESP8266 Master (doors)");
  Serial.println("  Serial3: ESP8266 Master (garden)");
  
  // Initialize sensors
  initializeSensors();
  
  // Initialize door devices
  initializeDevices();
  
  Serial.println("[INIT] ✓ Mega Garden Hub Ready");
  Serial.println("=====================================\n");
  
  systemUptime = millis();
}

void initializeSensors() {
  Serial.println("[SENSORS] Initializing garden sensors...");
  
  dht.begin();
  Serial.println("[SENSORS] ✓ DHT11 initialized");
  
  if (!rtc.begin()) {
    Serial.println("[SENSORS] ✗ MH RTC Module-2 not found");
  } else {
    Serial.println("[SENSORS] ✓ MH RTC Module-2 initialized");
  }
  
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN, LOW);
  Serial.println("[SENSORS] ✓ Pump relay initialized (OFF)");
  
  pinMode(WATER_SENSOR_PIN, INPUT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  
  gardenData.temperature = 0;
  gardenData.humidity = 0;
  gardenData.soilMoisture = 0;
  gardenData.lightLevel = 0;
  gardenData.rainDetected = false;
  gardenData.pumpRunning = false;
  gardenData.currentTime = "00:00:00";
  gardenData.lastUpdated = 0;
  
  Serial.println("[SENSORS] ✓ All garden sensors initialized");
}

void initializeDevices() {
  Serial.println("[INIT] Initializing door device database...");
  
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    devices[i].lastAction = "none";
    devices[i].doorOpen = false;
    devices[i].servoAngle = 0;
    devices[i].isOnline = false;
    devices[i].lastSeen = 0;
    devices[i].status = "offline";
    
    Serial.println("Door " + String(i + 1) + ": " + devices[i].serialNumber);
  }
  
  Serial.println("[INIT] ✓ " + String(TOTAL_DEVICES) + " doors initialized");
}

void loop() {
  if (Serial1.available()) {
    String socketMessage = Serial1.readStringUntil('\n');
    handleSocketHubMessage(socketMessage);
  }
  
  if (Serial2.available()) {
    String doorMessage = Serial2.readStringUntil('\n');
    handleDoorMasterMessage(doorMessage);
  }
  
  if (Serial3.available()) {
    String gardenMessage = Serial3.readStringUntil('\n');
    handleGardenMasterMessage(gardenMessage);
  }
  
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 30000) {
    readGardenSensors();
    processGardenAutomation();
    sendGardenDataToESP();
    lastSensorRead = millis();
  }
  
  checkPumpSafety();
  
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 30000) {
    checkSystemHealth();
    lastStatusCheck = millis();
  }
  
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 120000) {
    printSystemStatus();
    lastStatusPrint = millis();
  }
  
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {
    sendSystemHeartbeat();
    lastHeartbeat = millis();
  }
  
  delay(100);
}

void readGardenSensors() {
  sensorReadings++;
  
  gardenData.temperature = dht.readTemperature();
  gardenData.humidity = dht.readHumidity();
  
  int soilRaw = analogRead(SOIL_MOISTURE_PIN);
  gardenData.soilMoisture = map(soilRaw, 1023, 0, 0, 100);
  
  bool lightDigital = digitalRead(LIGHT_SENSOR_PIN);
  gardenData.lightLevel = lightDigital ? 0 : 1000;
  
  int waterRaw = analogRead(WATER_SENSOR_PIN);
  gardenData.rainDetected = (waterRaw > 500);
  
  DateTime now = rtc.now();
  gardenData.currentTime = String(now.hour()) + ":" + 
                          (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":" +
                          (now.second() < 10 ? "0" : "") + String(now.second());
  
  gardenData.lastUpdated = millis();
  
  Serial.println("[SENSORS] Temp: " + String(gardenData.temperature) + "°C | " +
                "Humidity: " + String(gardenData.humidity) + "% | " +
                "Soil: " + String(gardenData.soilMoisture) + "% | " +
                "Light: " + String(gardenData.lightLevel) + " lux | " +
                "Rain: " + String(gardenData.rainDetected ? "YES" : "NO"));
}

void processGardenAutomation() {
  if (!autoWateringEnabled) return;
  
  if (gardenData.rainDetected && gardenData.pumpRunning) {
    stopPump("Rain detected");
    return;
  }
  
  if (gardenData.soilMoisture < soilMoistureThreshold && 
      !gardenData.pumpRunning && 
      !gardenData.rainDetected) {
    startPump("Low soil moisture");
  }
  
  if (gardenData.soilMoisture > (soilMoistureThreshold + 20) && 
      gardenData.pumpRunning) {
    stopPump("Soil moisture sufficient");
  }
}

void startPump(String reason) {
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  gardenData.pumpRunning = true;
  pumpStartTime = millis();
  
  Serial.println("[PUMP] ✓ Started - " + reason);
  sendPumpStatusToESP();
}

void stopPump(String reason) {
  digitalWrite(PUMP_RELAY_PIN, LOW);
  gardenData.pumpRunning = false;
  pumpStartTime = 0;
  
  Serial.println("[PUMP] ✓ Stopped - " + reason);
  sendPumpStatusToESP();
}

void checkPumpSafety() {
  if (gardenData.pumpRunning && pumpStartTime > 0 && 
      (millis() - pumpStartTime > maxPumpRunTime)) {
    stopPump("Safety timeout");
  }
}

void sendGardenDataToESP() {
  String data = "GARDEN_DATA:";
  data += String(gardenData.temperature) + ",";
  data += String(gardenData.humidity) + ",";
  data += String(gardenData.soilMoisture) + ",";
  data += String(gardenData.lightLevel) + ",";
  data += String(gardenData.rainDetected ? 1 : 0) + ",";
  data += String(gardenData.pumpRunning ? 1 : 0) + ",";
  data += gardenData.currentTime;
  
  Serial3.println(data);
  Serial.println("[MEGA→GARDEN] " + data);
}

void sendPumpStatusToESP() {
  String status = "PUMP_STATUS:";
  status += String(gardenData.pumpRunning ? 1 : 0);
  Serial3.println(status);
}

void handleSocketHubMessage(String message) {
  message.trim();
  if (message.length() == 0) return;
  
  socketHubConnected = true;
  lastSocketMessage = millis();
  
  Serial.println("[SOCKET→MEGA] " + message);
  
  if (message.startsWith("CMD:")) {
    if (message.indexOf("GARDEN") >= 0) {
      handleGardenSocketCommand(message);
    } else {
      handleDoorSocketCommand(message);
    }
  }
}

void handleGardenSocketCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4);
  
  if (cmdData == "GARDEN:PUMP_ON") {
    startPump("Manual command");
  } else if (cmdData == "GARDEN:PUMP_OFF") {
    stopPump("Manual command");
  } else if (cmdData == "GARDEN:AUTO_TOGGLE") {
    autoWateringEnabled = !autoWateringEnabled;
    Serial.println("[GARDEN] Auto watering: " + String(autoWateringEnabled ? "ON" : "OFF"));
  }
  
  String response = "RESP:{\"success\":true,\"command\":\"" + cmdData + "\",\"type\":\"garden\"}";
  Serial1.println(response);
}

void handleDoorSocketCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4);
  int colonIndex = cmdData.indexOf(':');
  if (colonIndex <= 0) {
    Serial.println("[CMD] ✗ Invalid door format: " + cmdMessage);
    return;
  }
  
  String serialNumber = cmdData.substring(0, colonIndex);
  String action = cmdData.substring(colonIndex + 1);
  
  // Kiểm tra độ dài dữ liệu để tránh cắt ngắn
  if (serialNumber.length() >= 32) {
    Serial.println("[CMD] ✗ Serial number too long: " + serialNumber);
    sendErrorResponse(serialNumber, action, "Serial number too long");
    return;
  }
  if (action.length() >= 16) {
    Serial.println("[CMD] ✗ Action too long: " + action);
    sendErrorResponse(serialNumber, action, "Action too long");
    return;
  }
  
  int deviceIndex = findDeviceBySerial(serialNumber);
  if (deviceIndex < 0) {
    Serial.println("[CMD] ✗ Door not found: " + serialNumber);
    sendErrorResponse(serialNumber, action, "Device not found");
    return;
  }
  
  devices[deviceIndex].lastAction = action;
  devices[deviceIndex].lastSeen = millis();
  
  // Định dạng lệnh theo cấu trúc ESPNowMessage
  String forwardCmd = "CMD:" + serialNumber + ":" + action;
  Serial2.println(forwardCmd);
  
  commandsProcessed++;
  
  Serial.println("[CMD] ✓ Door " + String(deviceIndex + 1) + ": " + action);
  Serial.println("[MEGA→DOOR_MASTER] " + forwardCmd);
}

void handleDoorMasterMessage(String message) {
  message.trim();
  if (message.length() == 0) return;
  
  doorMasterConnected = true;
  lastDoorMessage = millis();
  
  Serial.println("[DOOR_MASTER→MEGA] " + message);
  
  if (message.startsWith("RESP:")) {
    handleDoorMasterResponse(message);
  }
}

void handleDoorMasterResponse(String respMessage) {
  String jsonData = respMessage.substring(5);
  
  String deviceId = extractJsonField(jsonData, "deviceId");
  String command = extractJsonField(jsonData, "command");
  String success = extractJsonField(jsonData, "success");
  String result = extractJsonField(jsonData, "result");
  String servoAngle = extractJsonField(jsonData, "servo_angle");
  
  int deviceIndex = findDeviceBySerial(deviceId);
  if (deviceIndex >= 0) {
    devices[deviceIndex].isOnline = true;
    devices[deviceIndex].lastSeen = millis();
    devices[deviceIndex].status = result;
    
    if (servoAngle != "") {
      devices[deviceIndex].servoAngle = servoAngle.toInt();
      devices[deviceIndex].doorOpen = (devices[deviceIndex].servoAngle > 90);
    }
    
    Serial.println("[STATE] Door " + String(deviceIndex + 1) + " (" + deviceId + "):");
    Serial.println("        Action: " + command + " | Success: " + success);
    Serial.println("        Angle: " + servoAngle + "° | Door: " + String(devices[deviceIndex].doorOpen ? "OPEN" : "CLOSED"));
  }
  
  Serial1.println(respMessage);
  responsesForwarded++;
  
  Serial.println("[MEGA→SOCKET] " + respMessage);
}

void handleGardenMasterMessage(String message) {
  message.trim();
  if (message.length() == 0) return;
  
  gardenMasterConnected = true;
  lastGardenMessage = millis();
  
  Serial.println("[GARDEN_MASTER→MEGA] " + message);
  
  if (message.startsWith("RESP:")) {
    Serial1.println(message);
    Serial.println("[MEGA→SOCKET] " + message);
  }
}

int findDeviceBySerial(String serialNumber) {
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

String extractJsonField(String json, String fieldName) {
  String searchKey = "\"" + fieldName + "\":";
  int startIndex = json.indexOf(searchKey);
  if (startIndex == -1) return "";
  
  startIndex += searchKey.length();
  while (startIndex < json.length() && (json.charAt(startIndex) == ' ' || json.charAt(startIndex) == '"')) {
    startIndex++;
  }
  
  int endIndex = startIndex;
  while (endIndex < json.length() && json.charAt(endIndex) != ',' && json.charAt(endIndex) != '}' && json.charAt(endIndex) != '"') {
    endIndex++;
  }
  
  return json.substring(startIndex, endIndex);
}

void sendErrorResponse(String serialNumber, String action, String error) {
  // Kiểm tra độ dài chuỗi
  char serialBuf[32];
  char actionBuf[16];
  char errorBuf[32];
  safeStringCopy(serialBuf, serialNumber.c_str(), sizeof(serialBuf));
  safeStringCopy(actionBuf, action.c_str(), sizeof(actionBuf));
  safeStringCopy(errorBuf, error.c_str(), sizeof(errorBuf));
  
  String json = "{";
  json += "\"success\":false,";
  json += "\"result\":\"" + String(errorBuf) + "\",";
  json += "\"deviceId\":\"" + String(serialBuf) + "\",";
  json += "\"command\":\"" + String(actionBuf) + "\",";
  json += "\"mega_processed\":true,";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  Serial1.println("RESP:" + json);
}

void checkSystemHealth() {
  if (socketHubConnected && (millis() - lastSocketMessage > 120000)) {
    socketHubConnected = false;
    Serial.println("[HEALTH] ✗ Socket Hub timeout");
  }
  
  if (doorMasterConnected && (millis() - lastDoorMessage > 120000)) {
    doorMasterConnected = false;
    Serial.println("[HEALTH] ✗ Door Master timeout");
  }
  
  if (gardenMasterConnected && (millis() - lastGardenMessage > 120000)) {
    gardenMasterConnected = false;
    Serial.println("[HEALTH] ✗ Garden Master timeout");
  }
  
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].isOnline && (millis() - devices[i].lastSeen > 300000)) {
      devices[i].isOnline = false;
      devices[i].status = "timeout";
      Serial.println("[HEALTH] ✗ Door " + String(i + 1) + " timeout");
    }
  }
}

void sendSystemHeartbeat() {
  Serial.println("[HEARTBEAT] Mega Garden Hub alive - uptime: " + String((millis() - systemUptime) / 1000) + "s");
  Serial.println("           Sensors: " + String(sensorReadings) + " readings");
  Serial.println("           Garden: " + String(gardenData.temperature) + "°C, " + 
                String(gardenData.soilMoisture) + "% soil, pump " + 
                String(gardenData.pumpRunning ? "ON" : "OFF"));
}

void printSystemStatus() {
  Serial.println("\n======= MEGA GARDEN HUB STATUS =======");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Uptime: " + String((millis() - systemUptime) / 1000) + " seconds");
  Serial.println("Commands: " + String(commandsProcessed) + " | Responses: " + String(responsesForwarded));
  Serial.println("Sensor Readings: " + String(sensorReadings));
  
  Serial.println("\n--- Connections ---");
  Serial.println("Socket Hub: " + String(socketHubConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Door Master: " + String(doorMasterConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Garden Master: " + String(gardenMasterConnected ? "CONNECTED" : "DISCONNECTED"));
  
  Serial.println("\n--- Garden Status ---");
  Serial.println("Temperature: " + String(gardenData.temperature) + "°C");
  Serial.println("Humidity: " + String(gardenData.humidity) + "%");
  Serial.println("Soil Moisture: " + String(gardenData.soilMoisture) + "%");
  Serial.println("Light Level: " + String(gardenData.lightLevel) + " lux");
  Serial.println("Rain: " + String(gardenData.rainDetected ? "DETECTED" : "NONE"));
  Serial.println("Pump: " + String(gardenData.pumpRunning ? "RUNNING" : "STOPPED"));
  Serial.println("Auto Watering: " + String(autoWateringEnabled ? "ENABLED" : "DISABLED"));
  Serial.println("Time: " + gardenData.currentTime);
  
  Serial.println("\n--- Door Devices ---");
  int onlineCount = 0;
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].isOnline) onlineCount++;
    
    Serial.println("Door " + String(i + 1) + ": " + 
                  String(devices[i].isOnline ? "ONLINE" : "OFFLINE") + 
                  " | " + String(devices[i].doorOpen ? "OPEN" : "CLOSED") + 
                  " | " + String(devices[i].servoAngle) + "° | " + 
                  devices[i].status);
  }
  
  Serial.println("\nOnline Doors: " + String(onlineCount) + "/" + String(TOTAL_DEVICES));
  Serial.println("Free RAM: " + String(freeMemory()) + " bytes");
  Serial.println("=====================================\n");
}

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}