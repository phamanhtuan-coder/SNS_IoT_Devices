// ========================================
// UPDATED MEGA HUB - ESP8266 Door Hub + PCA9685 Support
// ========================================
#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>

#define FIRMWARE_VERSION "2.3.0"
#define HUB_ID "MEGA_HUB_GARDEN_001"

// ===== ARDUINO MEGA CENTRAL HUB =====
// Serial0 (0,1): Debug Output / USB
// Serial1 (18,19): ESP8266 Socket Hub
// Serial2 (16,17): ESP8266 Door Hub (PCA9685)
// Serial3 (14,15): ESP8266 Garden Hub (LCD + RGB)

// ===== GARDEN SENSOR PINS =====
#define DHT_PIN 22
#define DHT_TYPE DHT11
#define WATER_SENSOR_PIN A0
#define LIGHT_SENSOR_PIN 24
#define SOIL_MOISTURE_PIN A2
#define PUMP_RELAY_PIN 23

// ===== RGB LED PINS =====
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
  STATUS_GOOD = 0,
  STATUS_WATERING = 1,
  STATUS_DRY = 2,
  STATUS_ERROR = 3,
  STATUS_RAIN = 4,
  STATUS_NIGHT = 5,
  STATUS_OFFLINE = 6
};

// ===== SENSOR OBJECTS =====
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;

// ===== RELAY DEVICE DATA =====
struct RelayDevice {
  String serialNumber;
  String deviceName;
  int relayPin;
  bool isOn;
  bool canToggle;
  bool fireOverride;
  unsigned long lastToggle;
  String status;
};

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
  bool fireDetected;
  unsigned long lastUpdated;
};

GardenData gardenData;

// ===== DOOR DEVICE STATE MANAGEMENT =====
struct DeviceState {
  String serialNumber;
  String lastAction;
  bool doorOpen;
  int servoAngle;
  int pwmChannel;     // ✅ NEW: PCA9685 channel
  bool isOnline;
  unsigned long lastSeen;
  String status;
  String doorType;    // ✅ NEW: SERVO_PCA9685, DUAL_PCA9685
};

// ✅ UPDATED: Door database for ESP8266 Door Hub with PCA9685
DeviceState devices[9] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", "", false, 0, 0, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", "", false, 0, 1, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", "", false, 0, 2, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", "", false, 0, 3, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", "", false, 0, 4, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", "", false, 0, 5, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", "", false, 0, 6, false, 0, "offline", "DUAL_PCA9685"},
  {"SERL27JUN2501JYR2RKVTH6PWR9ETXC2", "", false, 0, 7, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVVSBGRTM0TRFW", "", false, 0, 8, false, 0, "offline", "SERVO_PCA9685"}
};

const int TOTAL_DEVICES = 9;

// ===== SYSTEM STATUS =====
bool socketHubConnected = false;
bool doorHubConnected = false;      // ✅ RENAMED: doorMasterConnected -> doorHubConnected
bool gardenHubConnected = false;    // ✅ RENAMED: gardenMasterConnected -> gardenHubConnected
unsigned long lastSocketMessage = 0;
unsigned long lastDoorMessage = 0;
unsigned long lastGardenMessage = 0;

// ===== AUTOMATION SETTINGS =====
int soilMoistureThreshold = 30;
int lightThreshold = 200;
bool autoWateringEnabled = true;
unsigned long pumpStartTime = 0;
const unsigned long maxPumpRunTime = 300000;

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

// ===== FUNCTION PROTOTYPES =====
void initializeSensors();
void initializeDevices();
void initializeRGB();
void initializeRelays();
void readGardenSensors();
void processGardenAutomation();
void startPump(String reason);
void stopPump(String reason);
void checkPumpSafety();
void sendGardenDataToESP();
void updateRGBStatus();
GardenStatus determineGardenStatus();
void setRGBColor(int red, int green, int blue);
void handleRGBTest();
void toggleRelay(int deviceIndex, String reason);
void setRelayState(int deviceIndex, bool state, String reason);
int findRelayDeviceBySerial(String serialNumber);
void handleRelayCommand(String serialNumber, String action);
void checkFireAlarm();
void sendRelayStatusToSocket();
void sendRelayDeviceStatus(int deviceIndex);
void handleSocketHubMessage(String message);
void handleGardenSocketCommand(String cmdMessage);
void handleDoorSocketCommand(String cmdMessage);
void handleRelaySocketCommand(String cmdMessage);
void handleDoorHubMessage(String message);     // ✅ RENAMED
void handleDoorHubResponse(String respMessage); // ✅ RENAMED
void handleGardenHubMessage(String message);   // ✅ RENAMED
void handleGardenHubCommand(String message);   // ✅ RENAMED
void forwardCommand(String serialNumber, String action);
int findDeviceBySerial(String serialNumber);
String extractJsonField(String json, String fieldName);
void sendErrorResponse(String serialNumber, String action, String error);
void sendRelayResponse(String serialNumber, String action, bool success, String result);
void checkSystemHealth();
void sendSystemHeartbeat();
void printSystemStatus();
int freeMemory();

void setup() {
  // Initialize all serial ports
  Serial.begin(115200);   // Debug Output / USB
  Serial1.begin(115200);  // ESP8266 Socket Hub
  Serial2.begin(115200);  // ESP8266 Door Hub (PCA9685)
  Serial3.begin(115200);  // ESP8266 Garden Hub (LCD + RGB)

  delay(2000);

  Serial.println("\n=== ARDUINO MEGA GARDEN HUB v2.3.0 (ESP8266 + PCA9685 Support) ===");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Managing " + String(TOTAL_DEVICES) + " doors + Garden + RGB + " + String(TOTAL_RELAY_DEVICES) + " relays");
  Serial.println("Interfaces:");
  Serial.println("  Serial0: Debug Output / USB");
  Serial.println("  Serial1: ESP8266 Socket Hub");
  Serial.println("  Serial2: ESP8266 Door Hub (PCA9685)");
  Serial.println("  Serial3: ESP8266 Garden Hub (LCD + RGB)");

  initializeRGB();
  initializeRelays();
  initializeSensors();
  initializeDevices();

  Serial.println("[INIT] ✓ Mega Garden Hub Ready (ESP8266 + PCA9685 Door Control)");
  Serial.println("================================================================\n");

  systemUptime = millis();
}

void initializeRGB() {
  Serial.println("[RGB] Initializing RGB LED...");
  
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  
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
    digitalWrite(relayDevices[i].relayPin, HIGH);
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
    Serial.println("[SENSORS] ✗ RTC not found");
  } else {
    Serial.println("[SENSORS] ✓ RTC initialized");
  }

  pinMode(PUMP_RELAY_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN, LOW);
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
  Serial.println("[INIT] Initializing ESP8266 Door Hub device database...");

  for (int i = 0; i < TOTAL_DEVICES; i++) {
    devices[i].lastAction = "none";
    devices[i].doorOpen = false;
    devices[i].servoAngle = 0;
    devices[i].isOnline = false;
    devices[i].lastSeen = 0;
    devices[i].status = "offline";

    Serial.println("Door " + String(i + 1) + " (Ch" + String(devices[i].pwmChannel) + "): " + 
                   devices[i].serialNumber + " [" + devices[i].doorType + "]");
  }

  Serial.println("[INIT] ✓ " + String(TOTAL_DEVICES) + " doors initialized for PCA9685 control");
}

void loop() {
  // Handle serial communications
  if (Serial1.available()) {
    String socketMessage = Serial1.readStringUntil('\n');
    handleSocketHubMessage(socketMessage);
  }

  if (Serial2.available()) {
    String doorMessage = Serial2.readStringUntil('\n');
    handleDoorHubMessage(doorMessage);  // ✅ RENAMED
  }

  if (Serial3.available()) {
    String gardenMessage = Serial3.readStringUntil('\n');
    handleGardenHubMessage(gardenMessage);  // ✅ RENAMED
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

// ===== ESP8266 DOOR HUB MESSAGE HANDLING =====
void handleDoorHubMessage(String message) {
  if (message.length() == 0) return;
  
  message.trim();
  
  doorHubConnected = true;
  lastDoorMessage = millis();
  
  Serial.println("[ESP8266_DOOR_HUB→MEGA] " + message);
  
  // ✅ Handle different message types from ESP8266 Door Hub
  if (message.startsWith("DOOR_HUB_PCA9685_READY")) {
    Serial.println("[DOOR_HUB] ✓ ESP8266 Door Hub with PCA9685 is ready");
    
  } else if (message.startsWith("CHANNELS:")) {
    String channelInfo = message.substring(9);
    Serial.println("[DOOR_HUB] ✓ PCA9685 Channels: " + channelInfo);
    
  } else if (message.startsWith("RESP:")) {
    handleDoorHubResponse(message);
    
  } else if (message.startsWith("STS:")) {
    // Forward status to Socket Hub
    Serial1.println(message);
    Serial.println("[MEGA→SOCKET] " + message);
    
  } else if (message.startsWith("PCA9685_INFO:")) {
    Serial.println("[DOOR_HUB] PCA9685 Info: " + message);
    
  } else if (message.startsWith("STATUS_COMPLETE")) {
    Serial.println("[DOOR_HUB] ✓ Status update complete");
    
  } else if (message.startsWith("TEST_COMPLETE")) {
    Serial.println("[DOOR_HUB] ✓ Test sequence complete");
    
  } else if (message.startsWith("[")) {
    // Debug messages from ESP8266 Door Hub
    Serial.println("[DOOR_HUB_DEBUG] " + message);
    
  } else {
    Serial.println("[DOOR_HUB] Unknown: " + message);
  }
}

void handleDoorHubResponse(String respMessage) {
  if (!respMessage.startsWith("RESP:")) return;
  
  String jsonData = respMessage.substring(5);
  
  // Extract key fields
  String deviceId = extractJsonField(jsonData, "d");
  String success = extractJsonField(jsonData, "s");
  String result = extractJsonField(jsonData, "r");
  String channel = extractJsonField(jsonData, "ch");
  
  // ✅ Update device state for PCA9685 devices
  int deviceIndex = findDeviceBySerial(deviceId);
  if (deviceIndex >= 0) {
    devices[deviceIndex].isOnline = true;
    devices[deviceIndex].lastSeen = millis();
    devices[deviceIndex].status = (success == "1") ? "online" : "error";
    
    // ✅ Update servo angle from response
    String angle = extractJsonField(jsonData, "a");
    if (angle.length() > 0) {
      devices[deviceIndex].servoAngle = angle.toInt();
      devices[deviceIndex].doorOpen = (devices[deviceIndex].servoAngle > 45); // Assume >45° = open
    }
    
    Serial.println("[DOOR_UPDATE] Door " + String(deviceIndex + 1) + " (Ch" + channel + "): " + 
                   result + " at " + String(devices[deviceIndex].servoAngle) + "°");
  }
  
  // Forward response to Socket Hub
  Serial1.println(respMessage);
  responsesForwarded++;
  
  Serial.println("[MEGA→SOCKET] Door response forwarded");
}

// ===== ESP8266 GARDEN HUB MESSAGE HANDLING =====
void handleGardenHubMessage(String message) {
  message.trim();
  if (message.length() == 0) return;

  gardenHubConnected = true;
  lastGardenMessage = millis();

  Serial.println("[ESP8266_GARDEN_HUB→MEGA] " + message);

  if (message.startsWith("GARDEN_CMD:")) {
    handleGardenHubCommand(message);
  } else if (message.startsWith("RESP:")) {
    Serial1.println(message);
    Serial.println("[MEGA→SOCKET] Garden response: " + message);
  } else {
    Serial.println("[GARDEN_HUB] Info: " + message);
  }
}

void handleGardenHubCommand(String message) {
  String command = message.substring(11);
  
  if (command == "PUMP_ON") {
    startPump("ESP8266 Garden Hub command");
  } else if (command == "PUMP_OFF") {
    stopPump("ESP8266 Garden Hub command");
  } else if (command == "AUTO_TOGGLE") {
    autoWateringEnabled = !autoWateringEnabled;
    Serial.println("[GARDEN] Auto watering toggled: " + String(autoWateringEnabled ? "ON" : "OFF"));
  } else if (command == "RGB_TEST") {
    handleRGBTest();
  } else if (command == "REQUEST_DATA") {
    sendGardenDataToESP();
  }
}

// ===== SOCKET HUB MESSAGE HANDLING =====
void handleSocketHubMessage(String message) {
  if (message.length() == 0) return;
  
  message.trim();
  
  socketHubConnected = true;
  lastSocketMessage = millis();

  if (message.startsWith("CMD:")) {
    String cmdData = message.substring(4);
    
    if (cmdData.indexOf("GARDEN") >= 0) {
      handleGardenSocketCommand(message);
    } else if (cmdData.indexOf("RELAY") >= 0) {
      handleRelaySocketCommand(message);
    } else {
      // Door command - parse serialNumber:action format
      int colonIndex = cmdData.indexOf(':');
      if (colonIndex > 0 && colonIndex < cmdData.length() - 1) {
        String serialNumber = cmdData.substring(0, colonIndex);
        String action = cmdData.substring(colonIndex + 1);
        
        if (serialNumber.length() == 32 && serialNumber.startsWith("SERL")) {
          forwardCommand(serialNumber, action);
        }
      }
    }
  }
}

void forwardCommand(String serialNumber, String action) {
  Serial.println("\n[FORWARD] Processing command for ESP8266 Door Hub");
  Serial.println("[FORWARD] Serial: " + serialNumber);
  Serial.println("[FORWARD] Action: " + action);
  
  if (serialNumber.length() > 32) {
    Serial.println("[FORWARD] ✗ Serial too long: " + String(serialNumber.length()));
    sendErrorResponse(serialNumber, action, "Serial number too long");
    return;
  }
  
  int deviceIndex = findDeviceBySerial(serialNumber);
  if (deviceIndex < 0) {
    Serial.println("[FORWARD] ✗ Device not found");
    sendErrorResponse(serialNumber, action, "Device not found");
    return;
  }
  
  Serial.println("[FORWARD] Found device at index: " + String(deviceIndex) + 
                 " (PCA9685 Ch" + String(devices[deviceIndex].pwmChannel) + ")");
  
  devices[deviceIndex].lastAction = action;
  devices[deviceIndex].lastSeen = millis();
  
  // ✅ Send command to ESP8266 Door Hub
  String forwardCmd = "CMD:" + serialNumber + ":" + action;
  Serial2.println(forwardCmd);
  delay(10);
  
  commandsProcessed++;
  
  Serial.println("[FORWARD] ✓ Sent to ESP8266 Door Hub");
  Serial.println("[MEGA→ESP8266_DOOR_HUB] " + forwardCmd);
}

// ===== REST OF FUNCTIONS (Garden, Relay, System) =====
void readGardenSensors() {
  sensorReadings++;

  gardenData.temperature = dht.readTemperature();
  gardenData.humidity = dht.readHumidity();

  if (isnan(gardenData.temperature) || isnan(gardenData.humidity)) {
    Serial.println("[SENSORS] ✗ DHT read failed, using defaults");
    if (isnan(gardenData.temperature)) gardenData.temperature = 25.0;
    if (isnan(gardenData.humidity)) gardenData.humidity = 50.0;
  }

  int soilRaw = analogRead(SOIL_MOISTURE_PIN);
  gardenData.soilMoisture = map(soilRaw, 1023, 0, 0, 100);

  bool lightDigital = digitalRead(LIGHT_SENSOR_PIN);
  gardenData.lightLevel = lightDigital ? 0 : 1000;

  int waterRaw = analogRead(WATER_SENSOR_PIN);
  gardenData.rainDetected = (waterRaw > 500);

  gardenData.fireDetected = (gardenData.temperature > 45);

  DateTime now = rtc.now();
  gardenData.currentTime = String(now.hour()) + ":" + 
                          (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":" + 
                          (now.second() < 10 ? "0" : "") + String(now.second());

  gardenData.lastUpdated = millis();

  Serial.println("[SENSORS] Temp: " + String(gardenData.temperature, 1) + "°C | " + 
                "Humidity: " + String(gardenData.humidity, 1) + "% | " + 
                "Soil: " + String(gardenData.soilMoisture) + "% | " + 
                "Light: " + String(gardenData.lightLevel) + " lux | " + 
                "Rain: " + String(gardenData.rainDetected ? "YES" : "NO") + " | " +
                "Fire: " + String(gardenData.fireDetected ? "YES" : "NO"));
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

void startPump(String reason) {
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  gardenData.pumpRunning = true;
  pumpStartTime = millis();

  Serial.println("[PUMP] ✓ Started - " + reason);
  
  // ✅ Send pump status to Garden Hub
  Serial3.println("PUMP_STATUS:1");
}

void stopPump(String reason) {
  digitalWrite(PUMP_RELAY_PIN, LOW);
  gardenData.pumpRunning = false;
  pumpStartTime = 0;

  Serial.println("[PUMP] ✓ Stopped - " + reason);
  
  // ✅ Send pump status to Garden Hub
  Serial3.println("PUMP_STATUS:0");
}

void checkPumpSafety() {
  if (gardenData.pumpRunning && pumpStartTime > 0 && (millis() - pumpStartTime > maxPumpRunTime)) {
    stopPump("Safety timeout");
  }
}

void sendGardenDataToESP() {
  String data = "GARDEN_DATA:";
  data += String(gardenData.temperature, 1) + ",";
  data += String(gardenData.humidity, 1) + ",";
  data += String(gardenData.soilMoisture) + ",";
  data += String(gardenData.lightLevel) + ",";
  data += String(gardenData.rainDetected ? 1 : 0) + ",";
  data += String(gardenData.pumpRunning ? 1 : 0) + ",";
  data += gardenData.currentTime;

  Serial3.println(data);
  Serial.println("[MEGA→ESP8266_GARDEN_HUB] " + data);
}

// ===== RGB FUNCTIONS =====
void updateRGBStatus() {
  GardenStatus newStatus = determineGardenStatus();
  
  if (newStatus != currentGardenStatus) {
    currentGardenStatus = newStatus;
    lastRGBUpdate = millis();
  }
  
  switch(currentGardenStatus) {
    case STATUS_GOOD:
      setRGBColor(0, 255, 0);
      gardenData.rgbStatus = "Green-Good";
      break;
    case STATUS_WATERING:
      setRGBColor(0, 0, 255);
      gardenData.rgbStatus = "Blue-Watering";
      break;
    case STATUS_DRY:
      setRGBColor(255, 255, 0);
      gardenData.rgbStatus = "Yellow-Dry";
      break;
    case STATUS_ERROR:
      setRGBColor(255, 0, 0);
      gardenData.rgbStatus = "Red-Error";
      break;
    case STATUS_RAIN:
      setRGBColor(128, 0, 128);
      gardenData.rgbStatus = "Purple-Rain";
      break;
    case STATUS_NIGHT:
      setRGBColor(64, 64, 64);
      gardenData.rgbStatus = "White-Night";
      break;
    case STATUS_OFFLINE:
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
  if (gardenData.fireDetected) return STATUS_ERROR;
  if (!gardenHubConnected) return STATUS_OFFLINE;
  if (gardenData.rainDetected) return STATUS_RAIN;
  if (gardenData.pumpRunning) return STATUS_WATERING;
  if (gardenData.soilMoisture < soilMoistureThreshold) return STATUS_DRY;
  if (gardenData.lightLevel < lightThreshold) return STATUS_NIGHT;
  if (gardenData.temperature > 35 || gardenData.temperature < 0) return STATUS_ERROR;
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
  
  updateRGBStatus();
  Serial.println("[RGB] ✓ Test complete");
}

// ===== REMAINING FUNCTIONS (Relay, System, etc.) =====
// [Include all the relay functions, system functions from original code...]

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
  String json = "{";
  json += "\"success\":false,";
  json += "\"result\":\"" + error + "\",";
  json += "\"deviceId\":\"" + serialNumber + "\",";
  json += "\"command\":\"" + action + "\",";
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

  if (doorHubConnected && (millis() - lastDoorMessage > 120000)) {
    doorHubConnected = false;
    Serial.println("[HEALTH] ✗ ESP8266 Door Hub timeout");
  }

  if (gardenHubConnected && (millis() - lastGardenMessage > 120000)) {
    gardenHubConnected = false;
    Serial.println("[HEALTH] ✗ ESP8266 Garden Hub timeout");
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
  Serial.println("           Garden: " + String(gardenData.temperature, 1) + "°C, " + 
                 String(gardenData.soilMoisture) + "% soil, pump " + 
                 String(gardenData.pumpRunning ? "ON" : "OFF"));
  Serial.println("           RGB: " + gardenData.rgbStatus);
  Serial.println("           Doors: ESP8266 + PCA9685 (" + String(TOTAL_DEVICES) + " channels)");
  Serial.println("           Fire: " + String(gardenData.fireDetected ? "DETECTED" : "NONE"));
}

void printSystemStatus() {
  Serial.println("\n======= MEGA GARDEN HUB STATUS (ESP8266 + PCA9685) =======");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Version: " + String(FIRMWARE_VERSION));
  Serial.println("Uptime: " + String((millis() - systemUptime) / 1000) + " seconds");
  Serial.println("Commands: " + String(commandsProcessed) + " | Responses: " + String(responsesForwarded));
  Serial.println("Sensor Readings: " + String(sensorReadings));

  Serial.println("\n--- Hub Connections ---");
  Serial.println("Socket Hub: " + String(socketHubConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Door Hub (ESP8266 + PCA9685): " + String(doorHubConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Garden Hub (ESP8266 + LCD): " + String(gardenHubConnected ? "CONNECTED" : "DISCONNECTED"));

  Serial.println("\n--- Garden Status ---");
  Serial.println("Temperature: " + String(gardenData.temperature, 1) + "°C");
  Serial.println("Humidity: " + String(gardenData.humidity, 1) + "%");
  Serial.println("Soil Moisture: " + String(gardenData.soilMoisture) + "%");
  Serial.println("Pump: " + String(gardenData.pumpRunning ? "RUNNING" : "STOPPED"));
  Serial.println("RGB: " + gardenData.rgbStatus);

  Serial.println("\n--- Door Devices (ESP8266 + PCA9685) ---");
  int onlineCount = 0;
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].isOnline) onlineCount++;
    Serial.println("Door " + String(i + 1) + " (Ch" + String(devices[i].pwmChannel) + "): " + 
                   String(devices[i].isOnline ? "ONLINE" : "OFFLINE") + " | " + 
                   String(devices[i].doorOpen ? "OPEN" : "CLOSED") + " | " + 
                   String(devices[i].servoAngle) + "° | " + devices[i].status + " | " +
                   devices[i].doorType);
  }

  Serial.println("\nOnline Doors: " + String(onlineCount) + "/" + String(TOTAL_DEVICES));
  Serial.println("Free RAM: " + String(freeMemory()) + " bytes");
  Serial.println("==========================================================\n");
}

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// Include remaining relay functions and socket handling functions...
// [All relay functions remain the same as in original code]