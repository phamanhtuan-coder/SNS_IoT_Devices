#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>

#define FIRMWARE_VERSION "2.2.0"
#define HUB_ID "MEGA_HUB_GARDEN_001"

// ===== ARDUINO MEGA CENTRAL HUB WITH GARDEN SENSORS + RGB + RELAY CONTROL =====
// Serial0 (0,1): Debug Output / USB
// Serial1 (18,19): ESP8266 Socket Hub
// Serial2 (16,17): ESP8266 Master (doors)
// Serial3 (14,15): ESP8266 Master (garden)

// ===== GARDEN SENSOR PINS =====
#define DHT_PIN 22
#define DHT_TYPE DHT11
#define WATER_SENSOR_PIN A0
#define LIGHT_SENSOR_PIN 24  // Digital pin for LDR module
#define SOIL_MOISTURE_PIN A2
#define PUMP_RELAY_PIN 23

// ===== RGB LED PINS (Moved from ESP8266) =====
#define RGB_RED_PIN 5
#define RGB_GREEN_PIN 6
#define RGB_BLUE_PIN 7

// ===== 8-CHANNEL RELAY PINS =====
#define RELAY_1_PIN 30  // Fan control
#define RELAY_2_PIN 31  // Alarm control
#define RELAY_3_PIN 32  // Reserved
#define RELAY_4_PIN 33  // Reserved
#define RELAY_5_PIN 34  // Reserved
#define RELAY_6_PIN 35  // Reserved
#define RELAY_7_PIN 36  // Light 1
#define RELAY_8_PIN 37  // Light 2

// ===== RGB STATUS COLORS =====
enum GardenStatus {
  STATUS_GOOD = 0,     // Green - All good
  STATUS_WATERING = 1, // Blue - Watering in progress  
  STATUS_DRY = 2,      // Yellow - Low soil moisture
  STATUS_ERROR = 3,    // Red - Error/Alert
  STATUS_RAIN = 4,     // Purple - Rain detected
  STATUS_NIGHT = 5,    // White - Night mode
  STATUS_OFFLINE = 6   // Red blink - No connection
};

#define SERIAL_RX_BUFFER_SIZE 256

// ===== SENSOR OBJECTS =====
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;  // RTClib works with both DS3231 and DS1307 (MH RTC-2)

// ===== RELAY DEVICE DATA =====
struct RelayDevice {
  String serialNumber;
  String deviceName;
  int relayPin;
  bool isOn;
  bool canToggle;
  bool fireOverride;  // For alarm - can be manually overridden
  unsigned long lastToggle;
  String status;
};

// ===== RELAY DEVICES DEFINITION =====
RelayDevice relayDevices[8] = {
  { "RELAY27JUN2501FAN001CONTROL001", "Fan", RELAY_1_PIN, false, true, false, 0, "off" },
  { "RELAY27JUN2501ALARM01CONTROL01", "Alarm", RELAY_2_PIN, false, true, true, 0, "off" },
  { "RELAY27JUN2501RESERVED003CTRL1", "Reserved3", RELAY_3_PIN, false, true, false, 0, "off" },
  { "RELAY27JUN2501RESERVED004CTRL1", "Reserved4", RELAY_4_PIN, false, true, false, 0, "off" },
  { "RELAY27JUN2501RESERVED005CTRL1", "Reserved5", RELAY_5_PIN, false, true, false, 0, "off" },
  { "RELAY27JUN2501RESERVED006CTRL1", "Reserved6", RELAY_6_PIN, false, true, false, 0, "off" },
  { "RELAY27JUN2501LIGHT007CONTROL1", "Light1", RELAY_7_PIN, false, true, false, 0, "off" },
  { "RELAY27JUN2501LIGHT008CONTROL1", "Light2", RELAY_8_PIN, false, true, false, 0, "off" }
};

const int TOTAL_RELAY_DEVICES = 8;

// ===== GARDEN SENSOR DATA =====
struct GardenData {
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  String currentTime;
  String rgbStatus;
  bool fireDetected;  // New: Fire detection status
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

DeviceState devices[10] = {
  { "SERL27JUN2501JYR2RKVVX08V40YMGTW", "", false, 0, false, 0, "offline" },
  { "SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", "", false, 0, false, 0, "offline" },
  { "SERL27JUN2501JYR2RKVRNHS46VR6AS1", "", false, 0, false, 0, "offline" },
  { "SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", "", false, 0, false, 0, "offline" },
  { "SERL27JUN2501JYR2RKVTBZ40JPF88WP", "", false, 0, false, 0, "offline" },
  { "SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", "", false, 0, false, 0, "offline" },
  { "SERL27JUN2501JYR2RKVS2P6XBVF1P2E", "", false, 0, false, 0, "offline" }
};

const int TOTAL_DEVICES = 10;

DoorConfig doors[10] = {
  // Original 7 ESP-01 servo doors
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", "", false, 0, false, 0, "offline" },
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD","", false, 0, false, 0, "offline" },
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", "", false, 0, false, 0, "offline" },
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP","", false, 0, false, 0, "offline" },
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", "", false, 0, false, 0, "offline" },
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", "", false, 0, false, 0, "offline" },
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E","", false, 0, false, 0, "offline" },
  // Door ID 8: ESP-01 servo door (missing MAC - to be updated)
  {"SERL27JUN2501JYR2RKVTH6PWR9ETXC2", "", false, 0, false, 0, "offline" },
  // Door ID 9: ESP32 rolling door
  {"SERL27JUN2501JYR2RKVVSBGRTM0TRFW","", false, 0, false, 0, "offline" },
  // Door ID 10: Sliding door (placeholder - to be configured)
  {"", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},"", false, 0, false, 0, "offline" },
};
const int TOTAL_DOORS = 10;  // Only count active doors (1-7 + 9), skip 8 and 10 for now


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
const unsigned long maxPumpRunTime = 300000;  // 5 minutes max

// ===== RGB STATUS =====
GardenStatus currentGardenStatus = STATUS_OFFLINE;
unsigned long lastRGBUpdate = 0;
bool rgbBlinkState = false;

// ===== STATISTICS =====
unsigned long commandsProcessed = 0;
unsigned long responsesForwarded = 0;
unsigned long sensorReadings = 0;
unsigned long relayCommands = 0;
unsigned long systemUptime = 0;

// ===== SAFE STRING COPY HELPER =====
void safeStringCopy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';  // Ensure null termination
  }
}

// ===== FUNCTION PROTOTYPES =====
// Initialization functions
void initializeSensors();
void initializeDevices();
void initializeRGB();
void initializeRelays();

// Garden sensor functions
void readGardenSensors();
void processGardenAutomation();
void startPump(String reason);
void stopPump(String reason);
void checkPumpSafety();
void sendGardenDataToESP();
void sendPumpStatusToESP();
void sendRGBStatusToESP();

// RGB LED functions
void updateRGBStatus();
GardenStatus determineGardenStatus();
void setRGBColor(int red, int green, int blue);
void handleRGBTest();

// Relay control functions
void toggleRelay(int deviceIndex, String reason);
void setRelayState(int deviceIndex, bool state, String reason);
int findRelayDeviceBySerial(String serialNumber);
void handleRelayCommand(String serialNumber, String action);
void checkFireAlarm();
void sendRelayStatusToSocket();
void sendRelayDeviceStatus(int deviceIndex);

// Message handling functions
void handleSocketHubMessage(String message);
void handleEventMessage(String message);
void handleDirectCommand(String message);
void handleGardenSocketCommand(String cmdMessage);
void handleDoorSocketCommand(String cmdMessage);
void handleRelaySocketCommand(String cmdMessage);
void handleDoorMasterMessage(String message);
void handleDoorMasterResponse(String respMessage);
void handleGardenMasterMessage(String message);
void handleGardenMasterCommand(String message);

// Command processing functions
void forwardCommand(String serialNumber, String action);
int findDeviceBySerial(String serialNumber);
String extractJsonField(String json, String fieldName);
void sendErrorResponse(String serialNumber, String action, String error);
void sendRelayResponse(String serialNumber, String action, bool success, String result);

// System functions
void checkSystemHealth();
void sendSystemHeartbeat();
void printSystemStatus();
int freeMemory();

void setup() {
  // Initialize all serial ports
  Serial.begin(115200);   // Debug Output / USB
  Serial1.begin(115200);  // ESP8266 Socket Hub
  Serial2.begin(115200);  // ESP8266 Master (doors)
  Serial3.begin(115200);  // ESP8266 Master (garden)

  delay(2000);

  Serial.println("\n=== ARDUINO MEGA GARDEN HUB v2.2.0 (RGB + 8-Relay) ===");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Managing " + String(TOTAL_DEVICES) + " doors + Garden + RGB + " + String(TOTAL_RELAY_DEVICES) + " relays");
  Serial.println("Interfaces:");
  Serial.println("  Serial0: Debug Output / USB");
  Serial.println("  Serial1: ESP8266 Socket Hub");
  Serial.println("  Serial2: ESP8266 Master (doors)");
  Serial.println("  Serial3: ESP8266 Master (garden)");

  // Initialize RGB LED
  initializeRGB();

  // Initialize relays
  initializeRelays();

  // Initialize sensors
  initializeSensors();

  // Initialize door devices
  initializeDevices();

  Serial.println("[INIT] ✓ Mega Garden Hub Ready (RGB + 8-Relay Control)");
  Serial.println("================================================\n");

  systemUptime = millis();
}

void initializeRGB() {
  Serial.println("[RGB] Initializing RGB LED...");
  
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  
  // RGB test sequence
  setRGBColor(255, 0, 0); delay(300);
  setRGBColor(0, 255, 0); delay(300);
  setRGBColor(0, 0, 255); delay(300);
  setRGBColor(0, 0, 0);
  
  gardenData.rgbStatus = "Initialized";
  Serial.println("[RGB] ✓ LED ready on pins " + String(RGB_RED_PIN) + ", " + String(RGB_GREEN_PIN) + ", " + String(RGB_BLUE_PIN));
}

void initializeRelays() {
  Serial.println("[RELAY] Initializing 8-channel relay module...");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    pinMode(relayDevices[i].relayPin, OUTPUT);
    digitalWrite(relayDevices[i].relayPin, HIGH);  // Relay modules are usually active LOW
    relayDevices[i].isOn = false;
    relayDevices[i].lastToggle = 0;
    relayDevices[i].status = "off";
    
    Serial.println("[RELAY] " + relayDevices[i].deviceName + " (Pin " + String(relayDevices[i].relayPin) + "): " + relayDevices[i].serialNumber);
  }
  
  Serial.println("[RELAY] ✓ All 8 relays initialized (OFF state)");
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
  gardenData.rgbStatus = "Offline";
  gardenData.fireDetected = false;
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
  // Handle serial communications
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

  // Sensor reading and automation
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 30000) {
    readGardenSensors();
    processGardenAutomation();
    sendGardenDataToESP();
    lastSensorRead = millis();
  }

  // RGB LED updates
  static unsigned long lastRGBCheck = 0;
  if (millis() - lastRGBCheck > 1000) {
    updateRGBStatus();
    lastRGBCheck = millis();
  }

  // Fire alarm check
  static unsigned long lastFireCheck = 0;
  if (millis() - lastFireCheck > 5000) {
    checkFireAlarm();
    lastFireCheck = millis();
  }

  // Relay status broadcast
  static unsigned long lastRelayBroadcast = 0;
  if (millis() - lastRelayBroadcast > 60000) {
    sendRelayStatusToSocket();
    lastRelayBroadcast = millis();
  }

  // Safety and maintenance
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

  // Fire detection based on temperature (simple implementation)
  gardenData.fireDetected = (gardenData.temperature > 45);

  DateTime now = rtc.now();
  gardenData.currentTime = String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":" + (now.second() < 10 ? "0" : "") + String(now.second());

  gardenData.lastUpdated = millis();

  Serial.println("[SENSORS] Temp: " + String(gardenData.temperature) + "°C | " + "Humidity: " + String(gardenData.humidity) + "% | " + "Soil: " + String(gardenData.soilMoisture) + "% | " + "Light: " + String(gardenData.lightLevel) + " lux | " + "Rain: " + String(gardenData.rainDetected ? "YES" : "NO") + " | Fire: " + String(gardenData.fireDetected ? "YES" : "NO"));
}

void processGardenAutomation() {
  if (!autoWateringEnabled) return;

  if (gardenData.rainDetected && gardenData.pumpRunning) {
    stopPump("Rain detected");
    return;
  }

  if (gardenData.soilMoisture < soilMoistureThreshold && !gardenData.pumpRunning && !gardenData.rainDetected) {
    startPump("Low soil moisture");
  }

  if (gardenData.soilMoisture > (soilMoistureThreshold + 20) && gardenData.pumpRunning) {
    stopPump("Soil moisture sufficient");
  }
}

void checkFireAlarm() {
  static bool lastFireState = false;
  
  if (gardenData.fireDetected && !lastFireState) {
    // Fire detected - activate alarm (unless manually overridden)
    int alarmIndex = 1; // Alarm is relay device index 1
    if (!relayDevices[alarmIndex].fireOverride) {
      setRelayState(alarmIndex, true, "Fire detected");
      Serial.println("[FIRE] 🔥 ALARM ACTIVATED - Fire detected!");
    }
  }
  
  lastFireState = gardenData.fireDetected;
}

// ===== RELAY CONTROL FUNCTIONS =====
void toggleRelay(int deviceIndex, String reason) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  relayDevices[deviceIndex].isOn = !relayDevices[deviceIndex].isOn;
  digitalWrite(relayDevices[deviceIndex].relayPin, relayDevices[deviceIndex].isOn ? LOW : HIGH); // Active LOW
  relayDevices[deviceIndex].lastToggle = millis();
  relayDevices[deviceIndex].status = relayDevices[deviceIndex].isOn ? "on" : "off";
  
  relayCommands++;
  
  Serial.println("[RELAY] " + relayDevices[deviceIndex].deviceName + " TOGGLED to " + 
                String(relayDevices[deviceIndex].isOn ? "ON" : "OFF") + " - " + reason);
  
  // Send status update to socket
  sendRelayDeviceStatus(deviceIndex);
}

void setRelayState(int deviceIndex, bool state, String reason) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  relayDevices[deviceIndex].isOn = state;
  digitalWrite(relayDevices[deviceIndex].relayPin, state ? LOW : HIGH); // Active LOW
  relayDevices[deviceIndex].lastToggle = millis();
  relayDevices[deviceIndex].status = state ? "on" : "off";
  
  relayCommands++;
  
  Serial.println("[RELAY] " + relayDevices[deviceIndex].deviceName + " SET to " + 
                String(state ? "ON" : "OFF") + " - " + reason);
  
  // Send status update to socket
  sendRelayDeviceStatus(deviceIndex);
}

int findRelayDeviceBySerial(String serialNumber) {
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

void handleRelayCommand(String serialNumber, String action) {
  int deviceIndex = findRelayDeviceBySerial(serialNumber);
  
  if (deviceIndex < 0) {
    Serial.println("[RELAY] ✗ Device not found: " + serialNumber);
    sendRelayResponse(serialNumber, action, false, "Device not found");
    return;
  }
  
  bool success = false;
  String result = "ERR";
  
  if (action == "TOGGLE" && relayDevices[deviceIndex].canToggle) {
    toggleRelay(deviceIndex, "Manual toggle command");
    success = true;
    result = relayDevices[deviceIndex].isOn ? "ON" : "OFF";
    
  } else if (action == "ON") {
    setRelayState(deviceIndex, true, "Manual ON command");
    success = true;
    result = "ON";
    
  } else if (action == "OFF") {
    setRelayState(deviceIndex, false, "Manual OFF command");
    success = true;
    result = "OFF";
    
    // Special case: If turning off alarm manually, set fire override
    if (deviceIndex == 1) { // Alarm device
      relayDevices[deviceIndex].fireOverride = true;
      Serial.println("[ALARM] Fire override enabled - manual control");
    }
    
  } else if (action == "RESET_OVERRIDE" && deviceIndex == 1) {
    // Reset alarm fire override
    relayDevices[deviceIndex].fireOverride = false;
    success = true;
    result = "OVERRIDE_RESET";
    Serial.println("[ALARM] Fire override reset - automatic control restored");
    
  } else if (action == "STATUS") {
    success = true;
    result = relayDevices[deviceIndex].isOn ? "ON" : "OFF";
  }
  
  sendRelayResponse(serialNumber, action, success, result);
}

void sendRelayResponse(String serialNumber, String action, bool success, String result) {
  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"result\":\"" + result + "\",";
  json += "\"deviceId\":\"" + serialNumber + "\",";
  json += "\"command\":\"" + action + "\",";
  json += "\"type\":\"relay\",";
  json += "\"mega_processed\":true,";
  json += "\"timestamp\":" + String(millis());
  json += "}";

  Serial1.println("RESP:" + json);
  Serial.println("[MEGA→SOCKET] RELAY Response: " + json);
}

void sendRelayDeviceStatus(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  String statusMsg = "STS:RELAY:{";
  statusMsg += "\"deviceId\":\"" + relayDevices[deviceIndex].serialNumber + "\",";
  statusMsg += "\"name\":\"" + relayDevices[deviceIndex].deviceName + "\",";
  statusMsg += "\"state\":\"" + String(relayDevices[deviceIndex].isOn ? "on" : "off") + "\",";
  statusMsg += "\"pin\":" + String(relayDevices[deviceIndex].relayPin) + ",";
  statusMsg += "\"lastToggle\":" + String(relayDevices[deviceIndex].lastToggle) + ",";
  
  // Special fields for alarm
  if (deviceIndex == 1) {
    statusMsg += "\"fireOverride\":" + String(relayDevices[deviceIndex].fireOverride ? "true" : "false") + ",";
    statusMsg += "\"fireDetected\":" + String(gardenData.fireDetected ? "true" : "false") + ",";
  }
  
  statusMsg += "\"timestamp\":" + String(millis());
  statusMsg += "}";
  
  Serial1.println(statusMsg);
}

void sendRelayStatusToSocket() {
  Serial.println("[RELAY] Broadcasting status to socket...");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    sendRelayDeviceStatus(i);
    delay(50); // Small delay to prevent flooding
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
  if (gardenData.pumpRunning && pumpStartTime > 0 && (millis() - pumpStartTime > maxPumpRunTime)) {
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
  
  // Also send RGB status
  sendRGBStatusToESP();
}

void sendPumpStatusToESP() {
  String status = "PUMP_STATUS:";
  status += String(gardenData.pumpRunning ? 1 : 0);
  Serial3.println(status);
}

void sendRGBStatusToESP() {
  String rgbMessage = "RGB_STATUS:" + gardenData.rgbStatus;
  Serial3.println(rgbMessage);
}

// ===== RGB LED FUNCTIONS =====
void updateRGBStatus() {
  GardenStatus newStatus = determineGardenStatus();
  
  if (newStatus != currentGardenStatus) {
    currentGardenStatus = newStatus;
    lastRGBUpdate = millis();
  }
  
  switch(currentGardenStatus) {
    case STATUS_GOOD:
      setRGBColor(0, 255, 0);    // Green
      gardenData.rgbStatus = "Green-Good";
      break;
      
    case STATUS_WATERING:
      setRGBColor(0, 0, 255);    // Blue
      gardenData.rgbStatus = "Blue-Watering";
      break;
      
    case STATUS_DRY:
      setRGBColor(255, 255, 0);  // Yellow
      gardenData.rgbStatus = "Yellow-Dry";
      break;
      
    case STATUS_ERROR:
      setRGBColor(255, 0, 0);    // Red
      gardenData.rgbStatus = "Red-Error";
      break;
      
    case STATUS_RAIN:
      setRGBColor(128, 0, 128);  // Purple
      gardenData.rgbStatus = "Purple-Rain";
      break;
      
    case STATUS_NIGHT:
      setRGBColor(64, 64, 64);   // Dim white
      gardenData.rgbStatus = "White-Night";
      break;
      
    case STATUS_OFFLINE:
      // Blinking red
      if (millis() - lastRGBUpdate > 500) {
        rgbBlinkState = !rgbBlinkState;
        setRGBColor(rgbBlinkState ? 255 : 0, 0, 0);
        gardenData.rgbStatus = "Red-Blink-Offline";
        lastRGBUpdate = millis();
      }
      break;
  }
}

GardenStatus determineGardenStatus() {
  // Fire detection takes priority
  if (gardenData.fireDetected) {
    return STATUS_ERROR;
  }
  
  // Check system connections
  if (!gardenMasterConnected) {
    return STATUS_OFFLINE;
  }
  
  // Check environmental conditions
  if (gardenData.rainDetected) {
    return STATUS_RAIN;
  }
  
  if (gardenData.pumpRunning) {
    return STATUS_WATERING;
  }
  
  if (gardenData.soilMoisture < soilMoistureThreshold) {
    return STATUS_DRY;
  }
  
  if (gardenData.lightLevel < lightThreshold) {
    return STATUS_NIGHT;
  }
  
  if (gardenData.temperature > 35 || gardenData.temperature < 0) {
    return STATUS_ERROR;
  }
  
  return STATUS_GOOD;
}

void setRGBColor(int red, int green, int blue) {
  analogWrite(RGB_RED_PIN, red);
  analogWrite(RGB_GREEN_PIN, green);
  analogWrite(RGB_BLUE_PIN, blue);
}

void handleRGBTest() {
  Serial.println("[RGB] Running test sequence...");
  gardenData.rgbStatus = "Testing";
  
  setRGBColor(255, 0, 0); delay(500);
  setRGBColor(0, 255, 0); delay(500);
  setRGBColor(0, 0, 255); delay(500);
  setRGBColor(255, 255, 0); delay(500);
  setRGBColor(128, 0, 128); delay(500);
  setRGBColor(64, 64, 64); delay(500);
  
  // Return to current status
  updateRGBStatus();
  sendRGBStatusToESP();
  
  Serial.println("[RGB] ✓ Test complete");
}

void handleSocketHubMessage(String message) {
  if (message.length() == 0) return;
  
  // Trim and clean the message
  message.trim();
  
  socketHubConnected = true;
  lastSocketMessage = millis();

  // ✅ ONLY process clean CMD format - ignore debug messages
  if (message.startsWith("CMD:")) {
    String cmdData = message.substring(4);
    
    // Check command type based on serial number pattern
    if (cmdData.indexOf("GARDEN") >= 0) {
      handleGardenSocketCommand(message);
    } else if (cmdData.indexOf("RELAY") >= 0) {
      handleRelaySocketCommand(message);
    } else {
      // It's a door command - parse serialNumber:action format
      int colonIndex = cmdData.indexOf(':');
      if (colonIndex > 0 && colonIndex < cmdData.length() - 1) {
        String serialNumber = cmdData.substring(0, colonIndex);
        String action = cmdData.substring(colonIndex + 1);
        
        // Validate serial number format (should be 32 chars and start with SERL)
        if (serialNumber.length() == 32 && serialNumber.startsWith("SERL")) {
          forwardCommand(serialNumber, action);
        }
      }
    }
  }
  // ✅ IGNORE all debug messages that start with [
  else if (message.startsWith("[")) {
    return;
  }
  // ✅ IGNORE incomplete messages
  else if (message.length() < 10) {
    return;
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
  } else if (cmdData == "GARDEN:RGB_TEST") {
    handleRGBTest();
  }

  String response = "RESP:{\"success\":true,\"command\":\"" + cmdData + "\",\"type\":\"garden\",\"rgb_status\":\"" + gardenData.rgbStatus + "\"}";
  Serial1.println(response);
}

void handleRelaySocketCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4); // Remove "CMD:"
  
  // Parse RELAY_SERIAL:ACTION format
  int colonIndex = cmdData.indexOf(':');
  if (colonIndex <= 0) {
    Serial.println("[RELAY] ✗ Invalid relay format: " + cmdMessage);
    return;
  }
  
  String serialNumber = cmdData.substring(0, colonIndex);
  String action = cmdData.substring(colonIndex + 1);
  
  // Validate serial number format (should be 32 chars and start with RELAY)
  if (serialNumber.length() != 32 || !serialNumber.startsWith("RELAY")) {
    Serial.println("[RELAY] ✗ Invalid relay serial: " + serialNumber);
    sendRelayResponse(serialNumber, action, false, "Invalid serial format");
    return;
  }
  
  Serial.println("[RELAY] Processing: " + serialNumber + " -> " + action);
  handleRelayCommand(serialNumber, action);
}

void handleGardenMasterMessage(String message) {
  message.trim();
  if (message.length() == 0) return;

  gardenMasterConnected = true;
  lastGardenMessage = millis();

  Serial.println("[GARDEN_MASTER→MEGA] " + message);

  if (message.startsWith("GARDEN_CMD:")) {
    handleGardenMasterCommand(message);
  } else if (message.startsWith("RESP:")) {
    Serial1.println(message);
    Serial.println("[MEGA→SOCKET] " + message);
  }
}

void handleGardenMasterCommand(String message) {
  String command = message.substring(11); // Remove "GARDEN_CMD:"
  
  if (command == "PUMP_ON") {
    startPump("ESP8266 command");
  } else if (command == "PUMP_OFF") {
    stopPump("ESP8266 command");
  } else if (command == "AUTO_TOGGLE") {
    autoWateringEnabled = !autoWateringEnabled;
    Serial.println("[GARDEN] Auto watering toggled: " + String(autoWateringEnabled ? "ON" : "OFF"));
  } else if (command == "RGB_TEST") {
    handleRGBTest();
  } else if (command == "REQUEST_DATA") {
    sendGardenDataToESP();
  }
}

void handleDoorMasterMessage(String message) {
  if (message.length() == 0) return;
  
  doorMasterConnected = true;
  lastDoorMessage = millis();
  
  Serial.println("\n[DOOR_MASTER→MEGA] " + message);
  
  if (message.startsWith("RESP:")) {
    Serial.println("[DEBUG] Door response received");
    handleDoorMasterResponse(message);
  } else if (message.startsWith("STS:")) {
    Serial.println("[DEBUG] Door status received");
    Serial1.println(message);
    Serial.println("[MEGA→SOCKET] " + message);
  } else {
    Serial.println("[DEBUG] Unknown door message type");
  }
}

void handleDoorMasterResponse(String respMessage) {
  if (!respMessage.startsWith("RESP:")) return;
  
  String jsonData = respMessage.substring(5);
  
  if (jsonData.length() < 10 || !jsonData.endsWith("}")) {
    Serial.println("[RESP] Truncated response, requesting resend");
    return;
  }
  
  String deviceId = extractJsonField(jsonData, "d");
  String success = extractJsonField(jsonData, "s");
  
  int deviceIndex = findDeviceBySerial(deviceId);
  if (deviceIndex >= 0) {
    devices[deviceIndex].isOnline = true;
    devices[deviceIndex].lastSeen = millis();
    devices[deviceIndex].status = (success == "1") ? "online" : "error";
  }
  
  Serial1.println(respMessage);
  responsesForwarded++;
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
  Serial.println("           Garden: " + String(gardenData.temperature) + "°C, " + String(gardenData.soilMoisture) + "% soil, pump " + String(gardenData.pumpRunning ? "ON" : "OFF"));
  Serial.println("           RGB: " + gardenData.rgbStatus);
  Serial.println("           Relays: " + String(relayCommands) + " commands processed");
  Serial.println("           Fire: " + String(gardenData.fireDetected ? "DETECTED" : "NONE"));
}

void printSystemStatus() {
  Serial.println("\n======= MEGA GARDEN HUB STATUS (RGB + 8-Relay) =======");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Version: " + String(FIRMWARE_VERSION));
  Serial.println("Uptime: " + String((millis() - systemUptime) / 1000) + " seconds");
  Serial.println("Commands: " + String(commandsProcessed) + " | Responses: " + String(responsesForwarded));
  Serial.println("Sensor Readings: " + String(sensorReadings) + " | Relay Commands: " + String(relayCommands));

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
  Serial.println("Fire: " + String(gardenData.fireDetected ? "DETECTED" : "NONE"));
  Serial.println("Pump: " + String(gardenData.pumpRunning ? "RUNNING" : "STOPPED"));
  Serial.println("Auto Watering: " + String(autoWateringEnabled ? "ENABLED" : "DISABLED"));
  Serial.println("Time: " + gardenData.currentTime);

  Serial.println("\n--- RGB LED Status ---");
  Serial.println("Current Status: " + gardenData.rgbStatus);
  Serial.println("Garden Status: " + String(currentGardenStatus));
  Serial.println("RGB Pins: R=" + String(RGB_RED_PIN) + ", G=" + String(RGB_GREEN_PIN) + ", B=" + String(RGB_BLUE_PIN));

  Serial.println("\n--- 8-Channel Relay Status ---");
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    Serial.println("Relay " + String(i + 1) + " (" + relayDevices[i].deviceName + "): " + 
                   String(relayDevices[i].isOn ? "ON" : "OFF") + " | Pin " + 
                   String(relayDevices[i].relayPin) + " | " + relayDevices[i].serialNumber);
    
    if (i == 1) { // Alarm device
      Serial.println("  └─ Fire Override: " + String(relayDevices[i].fireOverride ? "YES" : "NO"));
    }
  }

  Serial.println("\n--- Door Devices ---");
  int onlineCount = 0;
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].isOnline) onlineCount++;
    Serial.println("Door " + String(i + 1) + ": " + String(devices[i].isOnline ? "ONLINE" : "OFFLINE") + " | " + String(devices[i].doorOpen ? "OPEN" : "CLOSED") + " | " + String(devices[i].servoAngle) + "° | " + devices[i].status);
  }

  Serial.println("\nOnline Doors: " + String(onlineCount) + "/" + String(TOTAL_DEVICES));
  Serial.println("Free RAM: " + String(freeMemory()) + " bytes");
  Serial.println("=======================================================\n");
}

void forwardCommand(String serialNumber, String action) {
  Serial.println("\n[FORWARD] Processing command");
  Serial.println("[FORWARD] Serial: " + serialNumber);
  Serial.println("[FORWARD] Action: " + action);
  
  if (serialNumber.length() > 32) {
    Serial.println("[FORWARD] ✗ Serial too long: " + String(serialNumber.length()));
    sendErrorResponse(serialNumber, action, "Serial number too long");
    return;
  }
  if (action.length() > 15) {
    Serial.println("[FORWARD] ✗ Action too long: " + String(action.length()));
    sendErrorResponse(serialNumber, action, "Action too long");
    return;
  }
  
  int deviceIndex = findDeviceBySerial(serialNumber);
  if (deviceIndex < 0) {
    Serial.println("[FORWARD] ✗ Device not found");
    sendErrorResponse(serialNumber, action, "Device not found");
    return;
  }
  
  Serial.println("[FORWARD] Found device at index: " + String(deviceIndex));
  
  devices[deviceIndex].lastAction = action;
  devices[deviceIndex].lastSeen = millis();
  
  String forwardCmd = "CMD:" + serialNumber + ":" + action;
  Serial2.println(forwardCmd);
  delay(10);
  
  commandsProcessed++;
  
  Serial.println("[FORWARD] ✓ Sent to Door Master");
  Serial.println("[MEGA→DOOR_MASTER] " + forwardCmd);
}

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}