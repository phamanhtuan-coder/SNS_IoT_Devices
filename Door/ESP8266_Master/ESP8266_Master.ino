#define FIRMWARE_VERSION "5.0.2"
#define DEVICE_TYPE "ESP8266_DOOR_HUB_PCA9685"
#define DEVICE_ID "SERL29JUN2501JYXECBS834760VJSFN5"

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// I2C PCA9685 Servo Controller
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// WiFi and WebSocket Configuration
String WIFI_SSID = "Anh Tuan";
String WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;
String DEVICE_SERIAL = "SERL29JUN2501JYXECBS834760VJSFN5";

// EEPROM Addresses
#define EEPROM_SIZE 512
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 164
#define WIFI_CONFIG_FLAG_ADDR 292

// WiFi Config
WiFiUDP udp;
ESP8266WebServer webServer(80);
const int UDP_PORT = 12345;
bool configMode = false;
unsigned long configModeStart = 0;
const unsigned long CONFIG_TIMEOUT = 300000; // 5 minutes
#define BUTTON_CONFIG_PIN 0  // GPIO0 for config mode

// Door Configuration
struct DoorConfig {
  String serialNumber;
  uint8_t channel;          // PCA9685 channel (0-15)
  uint16_t minPulse;        // Minimum pulse width (150)
  uint16_t maxPulse;        // Maximum pulse width (600)
  uint16_t closedAngle;     // Closed position angle
  uint16_t openAngle;       // Open position angle
  bool isDualDoor;          // Is this a dual door setup
  uint8_t secondChannel;    // Second servo channel for dual doors
  bool enabled;             // Is this door enabled
  String doorType;          // SERVO_PCA9685, DUAL_PCA9685
};

// Door Database
DoorConfig doors[9] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", 0, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", 1, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", 2, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", 3, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", 4, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", 5, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", 6, 150, 600, 0, 90, true, 7, true, "DUAL_PCA9685"},
  {"SERL27JUN2501JYR2RKVTH6PWR9ETXC2", 8, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL29JUN2501JYXECBS834760VJSFN5", 9, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"}
};

// Door State Enum
enum DoorState {
  DOOR_CLOSED,
  DOOR_OPENING,
  DOOR_OPEN,
  DOOR_CLOSING,
  DOOR_ERROR
};

// Door Status Structure
struct DoorStatus {
  DoorState state;
  uint16_t currentAngle;
  bool isMoving;
  unsigned long lastCommand;
  String lastAction;
  bool online;
};

DoorStatus doorStates[9];
WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("ESP8266 Door Hub PCA9685 v5.0.2");
  Serial.println("Device: " + String(DEVICE_ID));
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize I2C and PCA9685
  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(100);
  
  // Initialize doors
  for (int i = 0; i < 9; i++) {
    if (doors[i].enabled) {
      moveServoToAngle(i, doors[i].closedAngle);
      doorStates[i].state = DOOR_CLOSED;
      doorStates[i].currentAngle = doors[i].closedAngle;
      doorStates[i].isMoving = false;
      doorStates[i].lastCommand = millis();
      doorStates[i].lastAction = "INIT";
      doorStates[i].online = true;
      
      if (doors[i].isDualDoor) {
        moveSecondServoToAngle(i, doors[i].closedAngle);
      }
    }
  }
  
  // Check for config mode
  pinMode(BUTTON_CONFIG_PIN, INPUT_PULLUP);
  delay(100);
  if (digitalRead(BUTTON_CONFIG_PIN) == LOW) {
    Serial.println("[CONFIG] Config button pressed, entering WiFi config mode...");
    startConfigMode();
  } else {
    if (loadWiFiConfig()) {
      Serial.println("[CONFIG] Loaded WiFi config from EEPROM");
    } else {
      Serial.println("[CONFIG] No valid EEPROM config, using default");
    }
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      setupWebSocket();
    }
  }
  
  Serial.println("ESP8266 Door Hub Ready");
}

// WiFi Config Functions
void startConfigMode() {
  configMode = true;
  configModeStart = millis();
  
  String apName = String(DEVICE_TYPE) + "_" + String(DEVICE_ID);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str(), "12345678");
  
  IPAddress apIP = WiFi.softAPIP();
  Serial.println("[CONFIG] AP Started: SSID: " + apName + ", IP: " + apIP.toString());
  Serial.println("[CONFIG] Password: 12345678");
  Serial.println("[CONFIG] Web Interface: http://" + apIP.toString());
  Serial.println("[CONFIG] UDP Port: " + String(UDP_PORT));
  
  udp.begin(UDP_PORT);
  setupWebServer();
  webServer.begin();
}

void setupWebServer() {
  webServer.on("/", handleConfigPage);
  webServer.on("/save", HTTP_POST, handleSaveConfig);
  webServer.on("/status", HTTP_GET, handleStatus);
  webServer.onNotFound([]() {
    webServer.send(404, "text/plain", "Not found");
  });
}

void handleConfigPage() {
  String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset='UTF-8'>
        <title>ESP8266 WiFi Configuration</title>
    </head>
    <body>
        <h1>ESP8266 WiFi Configuration</h1>
        <form action='/save' method='POST'>
            WiFi SSID: <input type='text' name='ssid'><br>
            WiFi Password: <input type='password' name='password'><br>
            <input type='submit' value='Save'>
        </form>
    </body>
    </html>
  )";
  
  webServer.send(200, "text/html", html);
}

void handleSaveConfig() {
  String response;
  bool success = false;
  
  if (webServer.hasArg("ssid") && webServer.hasArg("password")) {
    String newSSID = webServer.arg("ssid");
    String newPassword = webServer.arg("password");
    
    if (newSSID.length() > 0 && newSSID.length() <= 31 && newPassword.length() <= 31) {
      saveWiFiConfig(newSSID, newPassword);
      response = "{\"success\":true,\"message\":\"Configuration saved\"}";
      success = true;
    } else {
      response = "{\"success\":false,\"message\":\"Invalid SSID or password length\"}";
    }
  } else {
    response = "{\"success\":false,\"message\":\"Missing SSID or password\"}";
  }
  
  webServer.send(200, "application/json", response);
  if (success) {
    delay(3000);
    ESP.restart();
  }
}

void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["device_type"] = DEVICE_TYPE;
  doc["device_serial"] = DEVICE_SERIAL;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  doc["socket_connected"] = socketConnected;
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void saveWiFiConfig(String ssid, String password) {
  if (ssid.length() > 31) ssid = ssid.substring(0, 31);
  if (password.length() > 31) password = password.substring(0, 31);
  
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, i < ssid.length() ? ssid[i] : 0);
    EEPROM.write(WIFI_PASS_ADDR + i, i < password.length() ? password[i] : 0);
  }
  EEPROM.write(WIFI_CONFIG_FLAG_ADDR, 0xAB);
  EEPROM.commit();
  Serial.println("[CONFIG] WiFi config saved: SSID=" + ssid);
}

bool loadWiFiConfig() {
  if (EEPROM.read(WIFI_CONFIG_FLAG_ADDR) != 0xAB) {
    Serial.println("[CONFIG] No valid EEPROM config found");
    return false;
  }
  
  char ssid[33] = {0};
  char password[33] = {0};
  
  for (int i = 0; i < 32; i++) {
    ssid[i] = EEPROM.read(WIFI_SSID_ADDR + i);
    password[i] = EEPROM.read(WIFI_PASS_ADDR + i);
  }
  
  WIFI_SSID = String(ssid);
  WIFI_PASSWORD = String(password);
  Serial.println("[CONFIG] Loaded SSID: " + WIFI_SSID);
  return true;
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  
  Serial.print("[WiFi] Connecting to: " + WIFI_SSID);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✓ CONNECTED");
    Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
    Serial.println("[WiFi] Signal: " + String(WiFi.RSSI()) + " dBm");
  } else {
    Serial.println(" ✗ FAILED");
    Serial.println("[WiFi] Starting config mode...");
    startConfigMode();
  }
}

void setupWebSocket() {
  // ✅ FIX: Hub detection parameters
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + DEVICE_SERIAL + 
                "&isIoTDevice=true&hub_managed=true&optimized=true&device_type=ESP_SOCKET_HUB";
  
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(25000, 5000, 2);
  
  // ✅ FIX: User-Agent for hub detection
  String userAgent = "ESP-Hub-Opt/5.0.2 ESP8266-Door-Hub";
  webSocket.setExtraHeaders(("User-Agent: " + userAgent).c_str());
  Serial.println("[WS] Setup complete - Hub Mode");
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] ✗ DISCONNECTED");
      socketConnected = false;
      break;
    case WStype_CONNECTED:
      Serial.println("[WS] ✓ CONNECTED");
      socketConnected = true;
      lastPingResponse = millis();
      sendDeviceOnline();
      break;
    case WStype_TEXT:
      handleWebSocketMessage(String((char*)payload));
      break;
    case WStype_PONG:
      lastPingResponse = millis();
      break;
    case WStype_ERROR:
      Serial.println("[WS] ✗ ERROR");
      break;
  }
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  if (type == '2') {
    webSocket.sendTXT("3");
    lastPingResponse = millis();
  } else if (type == '3') {
    lastPingResponse = millis();
  } else if (type == '4') {
    String socketIOData = message.substring(1);
    if (socketIOData.charAt(0) == '2') {
      handleSocketIOEvent(socketIOData.substring(1));
    }
  }
}

void handleSocketIOEvent(String eventData) {
  if (eventData.indexOf("command") != -1) {
    parseAndExecuteCommand(eventData);
  } else if (eventData.indexOf("config") != -1) {
    parseAndExecuteConfig(eventData);
  } else if (eventData.indexOf("status_request") != -1) {
    sendAllDoorStatus();
  } else if (eventData.indexOf("ping") != -1) {
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"device_serial\":\"" + DEVICE_SERIAL + "\"}]";
    webSocket.sendTXT(pongPayload);
    lastPingResponse = millis();
  }
}

void parseAndExecuteCommand(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  if (startIdx == -1 || endIdx == -1) return;
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, jsonString) != DeserializationError::Ok) return;
  
  String action = doc["action"].as<String>();
  String serialNumber = doc["serialNumber"].as<String>();
  
  int doorIndex = findDoorBySerial(serialNumber);
  if (doorIndex < 0) {
    Serial.println("[CMD] Door not found: " + serialNumber);
    return;
  }
  
  Serial.println("[CMD] Door " + String(doorIndex + 1) + ": " + action);
  
  doorStates[doorIndex].online = true;
  doorStates[doorIndex].lastCommand = millis();
  
  if (action == "open_door" || action == "OPN") {
    openDoor(doorIndex);
  } else if (action == "close_door" || action == "CLS") {
    closeDoor(doorIndex);
  } else if (action == "toggle_door" || action == "TGL") {
    toggleDoor(doorIndex);
  } else if (action == "get_config" || action == "CFG") {
    sendDoorConfig(doorIndex);
  } else if (action == "test_door") {
    testDoor(doorIndex);
  } else if (action == "get_status") {
    sendDoorStatus(doorIndex);
  } else {
    sendDoorResponse(doorIndex, action, false);
  }
}

void parseAndExecuteConfig(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  if (startIdx == -1 || endIdx == -1) return;
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, jsonString) != DeserializationError::Ok) return;
  
  String serialNumber = doc["serialNumber"].as<String>();
  int doorIndex = findDoorBySerial(serialNumber);
  if (doorIndex < 0) return;
  
  String configType = doc["config_type"].as<String>();
  if (configType == "servo_config") {
    int openAngle = doc["open_angle"] | doors[doorIndex].openAngle;
    int closedAngle = doc["closed_angle"] | doors[doorIndex].closedAngle;
    
    if (openAngle >= 0 && openAngle <= 180 && closedAngle >= 0 && closedAngle <= 180) {
      doors[doorIndex].openAngle = openAngle;
      doors[doorIndex].closedAngle = closedAngle;
      Serial.println("[CONFIG] Door " + String(doorIndex + 1) + " updated angles");
      sendConfigResponse(doorIndex, configType, true, "configured");
    } else {
      sendConfigResponse(doorIndex, configType, false, "invalid_angles");
    }
  }
}

// Door Control Functions
int findDoorBySerial(String serialNumber) {
  for (int i = 0; i < 9; i++) {
    if (doors[i].serialNumber == serialNumber) return i;
  }
  return -1;
}

void openDoor(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= 9 || !doors[doorIndex].enabled) return;
  if (doorStates[doorIndex].isMoving) return;
  
  Serial.println("[DOOR] Opening door " + String(doorIndex + 1));
  
  doorStates[doorIndex].state = DOOR_OPENING;
  doorStates[doorIndex].isMoving = true;
  doorStates[doorIndex].lastAction = "OPEN";
  doorStates[doorIndex].lastCommand = millis();
  
  moveServoToAngle(doorIndex, doors[doorIndex].openAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].openAngle);
  }
  
  delay(1000);
  
  doorStates[doorIndex].state = DOOR_OPEN;
  doorStates[doorIndex].currentAngle = doors[doorIndex].openAngle;
  doorStates[doorIndex].isMoving = false;
  
  sendDoorResponse(doorIndex, "open_door", true);
  sendDoorStatus(doorIndex);
}

void closeDoor(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= 9 || !doors[doorIndex].enabled) return;
  if (doorStates[doorIndex].isMoving) return;
  
  Serial.println("[DOOR] Closing door " + String(doorIndex + 1));
  
  doorStates[doorIndex].state = DOOR_CLOSING;
  doorStates[doorIndex].isMoving = true;
  doorStates[doorIndex].lastAction = "CLOSE";
  doorStates[doorIndex].lastCommand = millis();
  
  moveServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  }
  
  delay(1000);
  
  doorStates[doorIndex].state = DOOR_CLOSED;
  doorStates[doorIndex].currentAngle = doors[doorIndex].closedAngle;
  doorStates[doorIndex].isMoving = false;
  
  sendDoorResponse(doorIndex, "close_door", true);
  sendDoorStatus(doorIndex);
}

void toggleDoor(int doorIndex) {
  if (doorStates[doorIndex].state == DOOR_CLOSED) {
    openDoor(doorIndex);
  } else {
    closeDoor(doorIndex);
  }
}

void moveServoToAngle(int doorIndex, uint16_t angle) {
  uint16_t pulse = map(angle, 0, 180, doors[doorIndex].minPulse, doors[doorIndex].maxPulse);
  pwm.setPWM(doors[doorIndex].channel, 0, pulse);
}

void moveSecondServoToAngle(int doorIndex, uint16_t angle) {
  if (!doors[doorIndex].isDualDoor) return;
  uint16_t pulse = map(angle, 0, 180, doors[doorIndex].minPulse, doors[doorIndex].maxPulse);
  pwm.setPWM(doors[doorIndex].secondChannel, 0, pulse);
}

void testDoor(int doorIndex) {
  moveServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  }
  delay(1000);
  
  moveServoToAngle(doorIndex, doors[doorIndex].openAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].openAngle);
  }
  delay(1000);
  
  moveServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  }
  
  doorStates[doorIndex].state = DOOR_CLOSED;
  doorStates[doorIndex].currentAngle = doors[doorIndex].closedAngle;
  
  sendDoorResponse(doorIndex, "test_door", true);
}

// Communication Functions
void sendDeviceOnline() {
  StaticJsonDocument<300> doc;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["deviceType"] = "ESP_SOCKET_HUB";  // ✅ FIX: Correct hub type
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["door_type"] = "SERVO";
  doc["connection_type"] = "hub_managed";  // ✅ FIX: Hub connection type
  doc["managed_devices_count"] = 8;  // ✅ FIX: Managed devices count
  doc["hub_type"] = "ESP8266_PCA9685";  // ✅ FIX: Hub type info
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"device_online\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  Serial.println("[ONLINE] Hub registered with " + String(8) + " managed devices");
}

void sendDoorResponse(int doorIndex, String command, bool success) {
  StaticJsonDocument<250> doc;
  doc["success"] = success;
  doc["result"] = success ? "OK" : "ERR";
  doc["deviceId"] = doors[doorIndex].serialNumber;
  doc["command"] = command;
  doc["door_state"] = getStateString(doorIndex);
  doc["current_angle"] = doorStates[doorIndex].currentAngle;
  doc["door_type"] = "SERVO";  // ✅ FIX: Add door type
  doc["via_hub"] = DEVICE_SERIAL;  // ✅ FIX: Hub routing info
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"command_response\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
}

void sendDoorStatus(int doorIndex) {
  StaticJsonDocument<250> doc;
  doc["deviceId"] = doors[doorIndex].serialNumber;
  doc["door_state"] = getStateString(doorIndex);
  doc["current_angle"] = doorStates[doorIndex].currentAngle;
  doc["servo_angle"] = doorStates[doorIndex].currentAngle;  // ✅ FIX: Compatibility
  doc["is_moving"] = doorStates[doorIndex].isMoving;
  doc["door_type"] = "SERVO";  // ✅ FIX: Add door type
  doc["via_hub"] = DEVICE_SERIAL;  // ✅ FIX: Hub routing info
  doc["online"] = true;
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"deviceStatus\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
}

void sendAllDoorStatus() {
  Serial.println("[STATUS] Sending all door status...");
  for (int i = 0; i < 9; i++) {
    if (doors[i].enabled && doors[i].serialNumber != DEVICE_SERIAL) {  // ✅ FIX: Exclude hub itself
      sendDoorStatus(i);
      delay(50);
    }
  }
}

void sendDoorConfig(int doorIndex) {
  StaticJsonDocument<200> doc;
  doc["deviceId"] = doors[doorIndex].serialNumber;
  doc["config_type"] = "servo_config";
  doc["open_angle"] = doors[doorIndex].openAngle;
  doc["closed_angle"] = doors[doorIndex].closedAngle;
  doc["door_type"] = "SERVO";  // ✅ FIX: Add door type
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"config_response\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
}

void sendConfigResponse(int doorIndex, String configType, bool success, String result) {
  StaticJsonDocument<200> doc;
  doc["success"] = success;
  doc["result"] = result;
  doc["deviceId"] = doors[doorIndex].serialNumber;
  doc["config_type"] = configType;
  doc["door_type"] = "SERVO";  // ✅ FIX: Add door type
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"config_response\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
}

String getStateString(int doorIndex) {
  switch(doorStates[doorIndex].state) {
    case DOOR_CLOSED: return "closed";
    case DOOR_OPENING: return "opening";
    case DOOR_OPEN: return "open";
    case DOOR_CLOSING: return "closing";
    case DOOR_ERROR: return "error";
    default: return "unknown";
  }
}

void handleConfigMode() {
  if (!configMode) return;
  
  if (millis() - configModeStart > CONFIG_TIMEOUT) {
    Serial.println("[CONFIG] Timeout, restarting...");
    ESP.restart();
  }
  
  webServer.handleClient();
  handleUDPConfig();
}

void handleUDPConfig() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packet[256];
    int len = udp.read(packet, 255);
    packet[len] = 0;
    
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, packet) == DeserializationError::Ok) {
      String newSSID = doc["ssid"].as<String>();
      String newPassword = doc["password"].as<String>();
      
      if (newSSID.length() > 0 && newSSID.length() <= 31 && newPassword.length() <= 31) {
        saveWiFiConfig(newSSID, newPassword);
        Serial.println("[CONFIG] UDP config received, restarting...");
        delay(1000);
        ESP.restart();
      }
    }
  }
}

void checkConnectionStatus() {
  static unsigned long lastReconnectAttempt = 0;
  static int reconnectAttempts = 0;
  const int MAX_RECONNECT_ATTEMPTS = 5;
  const unsigned long RECONNECT_INTERVAL = 30000;
  
  if (!socketConnected && WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();
    
    if (now - lastReconnectAttempt < RECONNECT_INTERVAL) {
      return;
    }
    
    if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
      Serial.println("[RECONNECT] Max attempts reached, restarting...");
      delay(1000);
      ESP.restart();
    }
    
    Serial.println("[RECONNECT] Attempt " + String(reconnectAttempts + 1));
    
    webSocket.disconnect();
    delay(2000);
    
    if (reconnectAttempts >= 2) {
      WiFi.disconnect();
      delay(1000);
      setupWiFi();
    }
    
    setupWebSocket();
    lastReconnectAttempt = now;
    reconnectAttempts++;
  } else {
    reconnectAttempts = 0;
  }
}

void loop() {
  if (configMode) {
    handleConfigMode();
    return;
  }
  
  webSocket.loop();
  checkConnectionStatus();
  
  for (int i = 0; i < 9; i++) {
    if (doorStates[i].isMoving && (millis() - doorStates[i].lastCommand > 5000)) {
      doorStates[i].isMoving = false;
      doorStates[i].state = DOOR_ERROR;
      sendDoorStatus(i);
    }
  }
  
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate > 300000) {
    Serial.println("[HEALTH] Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("         Hub Type: ESP8266 PCA9685 Door Hub");
    Serial.println("         Managed Doors: 8 servos (Door 7 = dual)");
    Serial.println("         WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("         Signal: " + String(WiFi.RSSI()) + " dBm");
    }
    Serial.println("         Socket: " + String(socketConnected ? "Connected" : "Disconnected"));
    lastStatusUpdate = millis();
  }
  
  static unsigned long lastPing = 0;
  if (socketConnected && millis() - lastPing > 30000) {
    webSocket.sendTXT("2");
    lastPing = millis();
  }
  
  if (socketConnected && millis() - lastPingResponse > 120000) {
    Serial.println("[WS] No ping response, disconnecting...");
    webSocket.disconnect();
    socketConnected = false;
  }
  
  // Retry WiFi if disconnected
  static unsigned long lastWiFiRetry = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastWiFiRetry > 60000) {
    Serial.println("[WiFi] Disconnected, retrying...");
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      setupWebSocket();
    }
    lastWiFiRetry = millis();
  }
  
  delay(10);
}