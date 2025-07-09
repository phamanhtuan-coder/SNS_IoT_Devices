/**************************************************************
 * ESP8266 Fire Alarm - UDP WiFi + SSL Railway Connection
 * 
 * Version: v16.0 - UDP Config + SSL Support
 **************************************************************/

#define SERIAL_NUMBER "SERL12JUN2501JXHMC17J1RPRY7P063E"
#define DEVICE_ID "DEVICE111"

#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_AHTX0.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// Hotspot configuration
const char* HOTSPOT_SSID = "ESP8266-Alarm-Config";
const char* HOTSPOT_PASSWORD = "alarmconfig123";

// Server configuration (Railway SSL)
const char* WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
const uint16_t WEBSOCKET_PORT = 443;
const char* WEBSOCKET_PATH_TEMPLATE = "/socket.io/?EIO=3&transport=websocket&serialNumber=%s&isIoTDevice=true";

// Hardware
WebSocketsClient webSocket;
Adafruit_AHTX0 aht;
WiFiUDP udp;

#define MQ2_PIN A0
#define BUZZER_PIN_N D8
#define BUZZER_PIN D5
#define UDP_PORT 8888

// EEPROM configuration
#define EEPROM_SIZE 512
#define WIFI_SSID_ADDR 0
#define WIFI_PASS_ADDR 100
#define CONFIG_ADDR 200

// Network credentials
String wifiSSID = "";
String wifiPassword = "";

// State variables
bool isConnected = false;
bool namespaceConnected = false;
bool sensorAvailable = false;
bool alarmActive = false;
bool buzzerOverride = false;
bool isHotspotMode = false;
int reconnectAttempts = 0;

// Timing
unsigned long lastSensorUpdate = 0;
unsigned long lastPingTime = 0;
unsigned long SENSOR_INTERVAL = 10000;
const unsigned long PING_INTERVAL = 25000;

// Thresholds
int GAS_THRESHOLD = 600;
float TEMP_THRESHOLD = 40.0;
int SMOKE_THRESHOLD = 500;
unsigned long muteUntil = 0;

// Buffers
static char websocketPath[256];

// HTML Configuration Page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<title>ESP8266 Alarm Setup</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}
.container{max-width:500px;margin:0 auto;background:#fff;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}
h1{color:#333;text-align:center;margin-bottom:30px}
.form-group{margin-bottom:20px}
label{display:block;margin-bottom:5px;font-weight:bold;color:#555}
input[type="text"],input[type="password"],input[type="number"]{width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;font-size:16px;box-sizing:border-box}
input[type="submit"]{width:100%;padding:12px;background:#007bff;color:#fff;border:none;border-radius:5px;font-size:16px;cursor:pointer;transition:background 0.3s}
input[type="submit"]:hover{background:#0056b3}
.info{background:#e9ecef;padding:15px;border-radius:5px;margin-bottom:20px}
.status{text-align:center;margin-top:20px;padding:10px;border-radius:5px;font-weight:bold}
.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
</style>
</head><body>
<div class="container">
<h1>🚨 ESP8266 Alarm Setup</h1>
<div class="info">
<strong>Device:</strong> %s<br>
<strong>Version:</strong> v16.0<br>
<strong>Features:</strong> Fire/Gas Detection, Remote Control
</div>
<form action='/save_config' method='POST'>
<div class='form-group'>
<label for='ssid'>WiFi Network:</label>
<input type='text' id='ssid' name='ssid' required placeholder="Enter WiFi SSID">
</div>
<div class='form-group'>
<label for='password'>WiFi Password:</label>
<input type='password' id='password' name='password' placeholder="Enter WiFi Password">
</div>
<div class='form-group'>
<label for='gas_threshold'>Gas Threshold (300-1000):</label>
<input type='number' id='gas_threshold' name='gas_threshold' value='600' min='300' max='1000'>
</div>
<div class='form-group'>
<label for='temp_threshold'>Temperature Threshold (°C):</label>
<input type='number' id='temp_threshold' name='temp_threshold' value='40' min='20' max='80'>
</div>
<input type='submit' value='💾 Save Configuration'>
</form>
<div class="status">
<p>📡 Device will restart after saving configuration</p>
</div>
</div>
</body></html>
)rawliteral";

/**************************************************************
 * EEPROM FUNCTIONS
 **************************************************************/
void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  Serial.println("[EEPROM] Initialized");
}

String readEEPROMString(int address, int maxLength) {
  String result = "";
  result.reserve(maxLength);
  for (int i = 0; i < maxLength; i++) {
    char c = EEPROM.read(address + i);
    if (c == 0) break;
    result += c;
  }
  return result;
}

void writeEEPROMString(int address, const String& data, int maxLength) {
  int len = min((int)data.length(), maxLength - 1);
  for (int i = 0; i < len; i++) {
    EEPROM.write(address + i, data[i]);
  }
  EEPROM.write(address + len, 0);
  EEPROM.commit();
  Serial.printf("[EEPROM] Wrote string at %d: %s\n", address, data.c_str());
}

void saveConfiguration() {
  StaticJsonDocument<200> config;
  config["gas_threshold"] = GAS_THRESHOLD;
  config["temp_threshold"] = TEMP_THRESHOLD;
  config["smoke_threshold"] = SMOKE_THRESHOLD;
  config["sensor_interval"] = SENSOR_INTERVAL;
  
  String configStr;
  serializeJson(config, configStr);
  writeEEPROMString(CONFIG_ADDR, configStr, 200);
}

void loadConfiguration() {
  String configStr = readEEPROMString(CONFIG_ADDR, 200);
  if (configStr.length() > 0) {
    StaticJsonDocument<200> config;
    if (deserializeJson(config, configStr) == DeserializationError::Ok) {
      GAS_THRESHOLD = config["gas_threshold"] | 600;
      TEMP_THRESHOLD = config["temp_threshold"] | 40.0;
      SMOKE_THRESHOLD = config["smoke_threshold"] | 500;
      SENSOR_INTERVAL = config["sensor_interval"] | 10000;
      Serial.println("[Config] Loaded from EEPROM");
    }
  }
}

/**************************************************************
 * WIFI FUNCTIONS
 **************************************************************/
bool connectToWiFi(const String& ssid, const String& password) {
  Serial.printf("[WiFi] Connecting to %s...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Signal: %d dBm\n", WiFi.RSSI());
    return true;
  }
  
  Serial.println("[WiFi] Connection failed");
  return false;
}

void startHotspot() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  Serial.printf("[Hotspot] Started: %s\n", HOTSPOT_SSID);
  Serial.printf("[Hotspot] IP: %s\n", WiFi.softAPIP().toString().c_str());
  isHotspotMode = true;
}

/**************************************************************
 * UDP FUNCTIONS
 **************************************************************/
void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[512];
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = 0;
      Serial.printf("[UDP] Received: %s\n", packetBuffer);
      
      StaticJsonDocument<300> doc;
      DeserializationError error = deserializeJson(doc, packetBuffer);
      
      if (!error && doc.containsKey("ssid")) {
        String ssid = doc["ssid"].as<String>();
        String password = doc["password"].as<String>();
        int gasThreshold = doc["gas_threshold"] | 600;
        float tempThreshold = doc["temp_threshold"] | 40.0;
        
        Serial.println("[UDP] Received WiFi credentials and config");
        
        // Save WiFi credentials
        writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
        writeEEPROMString(WIFI_PASS_ADDR, password, 100);
        
        // Save configuration
        GAS_THRESHOLD = gasThreshold;
        TEMP_THRESHOLD = tempThreshold;
        saveConfiguration();
        
        // Send response
        StaticJsonDocument<150> response;
        response["status"] = "success";
        response["message"] = "Configuration received";
        response["device"] = SERIAL_NUMBER;
        
        String responseStr;
        serializeJson(response, responseStr);
        
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.print(responseStr);
        udp.endPacket();
        
        // Restart with new configuration
        delay(2000);
        ESP.restart();
      }
    }
  }
}

/**************************************************************
 * HTTP SERVER FUNCTIONS
 **************************************************************/
#include <ESP8266WebServer.h>
ESP8266WebServer httpServer(80);

void setupHttpServer() {
  // Configuration page
  httpServer.on("/", HTTP_GET, []() {
    char html[4000];
    snprintf(html, sizeof(html), CONFIG_HTML, SERIAL_NUMBER);
    httpServer.send(200, "text/html", html);
  });
  
  // Save configuration
  httpServer.on("/save_config", HTTP_POST, []() {
    String ssid = httpServer.arg("ssid");
    String password = httpServer.arg("password");
    int gasThreshold = httpServer.arg("gas_threshold").toInt();
    float tempThreshold = httpServer.arg("temp_threshold").toFloat();
    
    writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
    writeEEPROMString(WIFI_PASS_ADDR, password, 100);
    
    GAS_THRESHOLD = gasThreshold;
    TEMP_THRESHOLD = tempThreshold;
    saveConfiguration();
    
    httpServer.send(200, "text/html", 
      "<html><body style='font-family:Arial;text-align:center;padding:50px'>"
      "<h1>✅ Configuration Saved</h1>"
      "<p>Device will restart and connect to: <strong>" + ssid + "</strong></p>"
      "<p>Redirecting in 3 seconds...</p>"
      "<script>setTimeout(function(){window.location.href='/';}, 3000);</script>"
      "</body></html>");
    
    delay(2000);
    ESP.restart();
  });
  
  // Status API
  httpServer.on("/status", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["device"] = SERIAL_NUMBER;
    doc["status"] = alarmActive ? "alarm" : "normal";
    doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"] = WiFi.localIP().toString();
    doc["wifi"]["rssi"] = WiFi.RSSI();
    doc["thresholds"]["gas"] = GAS_THRESHOLD;
    doc["thresholds"]["temperature"] = TEMP_THRESHOLD;
    doc["uptime"] = millis() / 1000;
    
    String response;
    serializeJson(doc, response);
    
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", response);
  });
  
  httpServer.begin();
  Serial.println("[HTTP] Server started on port 80");
}

/**************************************************************
 * WEBSOCKET FUNCTIONS (SSL)
 **************************************************************/
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] Disconnected from server");
      isConnected = false;
      namespaceConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.printf("[WebSocket] Connected to server: %s\n", payload);
      Serial.println("[WebSocket] ✅ SSL WebSocket connection established");
      isConnected = true;
      namespaceConnected = false;
      reconnectAttempts = 0;
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[WebSocket] Message received: %s\n", message.c_str());
        handleWebSocketMessage(message);
        break;
      }
      
    case WStype_ERROR:
      Serial.printf("[WebSocket] Error: %s\n", payload);
      break;
      
    default:
      Serial.printf("[WebSocket] Unknown event type: %d\n", type);
      break;
  }
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char engineIOType = message.charAt(0);
  
  switch(engineIOType) {
    case '0': // Engine.IO OPEN
      Serial.println("[Engine.IO] OPEN - Session established");
      delay(1000);
      joinDeviceNamespace();
      break;
      
    case '2': // Engine.IO PING
      Serial.println("[Engine.IO] PING received");
      webSocket.sendTXT("3"); // Send PONG
      break;
      
    case '3': // Engine.IO PONG
      Serial.println("[Engine.IO] PONG received");
      break;
      
    case '4': // Engine.IO MESSAGE
      if (message.length() > 1) {
        String socketIOData = message.substring(1);
        handleSocketIOMessage(socketIOData);
      }
      break;
  }
}

void handleSocketIOMessage(String socketIOData) {
  if (socketIOData.length() < 1) return;
  
  char socketIOType = socketIOData.charAt(0);
  
  switch(socketIOType) {
    case '0': // Socket.IO CONNECT
      Serial.println("[Socket.IO] CONNECT acknowledged");
      if (socketIOData.indexOf("/device") != -1) {
        Serial.println("[Socket.IO] ✅ Connected to /device namespace!");
        namespaceConnected = true;
        delay(1000);
        sendDeviceOnline();
      }
      break;
      
    case '2': // Socket.IO EVENT
      parseSocketIOEvent(socketIOData.substring(1));
      break;
      
    case '4': // Socket.IO ERROR
      Serial.println("[Socket.IO] ERROR received");
      if (!namespaceConnected) {
        delay(2000);
        joinDeviceNamespace();
      }
      break;
  }
}

void parseSocketIOEvent(String eventData) {
  if (eventData.startsWith("/device,")) {
    eventData = eventData.substring(8);
  }
  
  int firstQuote = eventData.indexOf('"');
  if (firstQuote == -1) return;
  
  int secondQuote = eventData.indexOf('"', firstQuote + 1);
  if (secondQuote == -1) return;
  
  String eventName = eventData.substring(firstQuote + 1, secondQuote);
  
  int jsonStart = eventData.indexOf('{');
  if (jsonStart != -1) {
    String jsonData = eventData.substring(jsonStart, eventData.lastIndexOf('}') + 1);
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonData);
    
    if (!err) {
      handleCommand(eventName, doc);
    }
  }
}

void handleCommand(String eventName, JsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    
    if (action == "toggleBuzzer") {
      bool status = doc["state"]["power_status"];
      buzzerOverride = status;
      digitalWrite(BUZZER_PIN, status ? HIGH : LOW);
      sendCommandResponse("toggleBuzzer", true, "Buzzer " + String(status ? "activated" : "deactivated"));
    }
    else if (action == "resetAlarm") {
      alarmActive = false;
      buzzerOverride = false;
      digitalWrite(BUZZER_PIN, LOW);
      sendCommandResponse("resetAlarm", true, "Alarm reset successfully");
      sendDeviceStatus();
    }
    else if (action == "testAlarm") {
      triggerTestAlarm();
      sendCommandResponse("testAlarm", true, "Test alarm executed");
    }
    else if (action == "updateThreshold") {
      if (doc["config"].is<JsonObject>()) {
        JsonObject config = doc["config"];
        if (config["gas_threshold"].is<int>()) {
          GAS_THRESHOLD = config["gas_threshold"];
        }
        if (config["temp_threshold"].is<float>()) {
          TEMP_THRESHOLD = config["temp_threshold"];
        }
        saveConfiguration();
        sendCommandResponse("updateThreshold", true, "Thresholds updated successfully");
      }
    }
    else if (action == "muteAlarm") {
      int duration = doc["duration"] | 300;
      muteAlarmForDuration(duration);
      sendCommandResponse("muteAlarm", true, "Alarm muted for " + String(duration) + " seconds");
    }
  }
}

void joinDeviceNamespace() {
  Serial.println("[Socket.IO] Joining /device namespace...");
  webSocket.sendTXT("40/device,");
}

void sendSocketIOEvent(String eventName, String jsonData) {
  if (!namespaceConnected) return;
  
  String eventPayload = "42/device,[\"" + eventName + "\"," + jsonData + "]";
  webSocket.sendTXT(eventPayload);
}

void sendDeviceOnline() {
  if (!namespaceConnected) return;
  
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["deviceType"] = "FIRE_ALARM_SYSTEM";
  doc["firmware_version"] = "v16.0-UDP-SSL";
  doc["hardware_version"] = "ESP8266-v1.0";
  doc["capabilities"]["smoke_detection"] = true;
  doc["capabilities"]["temperature_monitoring"] = true;
  doc["capabilities"]["gas_detection"] = true;
  doc["capabilities"]["alarm_control"] = true;
  doc["esp8266_info"]["chip_id"] = String(ESP.getChipId(), HEX);
  doc["esp8266_info"]["free_heap"] = ESP.getFreeHeap();
  doc["esp8266_info"]["wifi_rssi"] = WiFi.RSSI();
  doc["esp8266_info"]["ip_address"] = WiFi.localIP().toString();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("device_online", jsonString);
}

void sendSensorData() {
  if (millis() - lastSensorUpdate < SENSOR_INTERVAL) return;
  if (!namespaceConnected) return;
  
  sensors_event_t humidity, temp;
  float temperature = 25.0;
  float humidityValue = 50.0;
  
  if (sensorAvailable && aht.getEvent(&humidity, &temp)) {
    temperature = temp.temperature;
    humidityValue = humidity.relative_humidity;
  } else {
    temperature = 25.0 + random(-5, 10);
    humidityValue = 50.0 + random(-10, 20);
  }
  
  int gasValue = analogRead(MQ2_PIN);
  
  bool shouldAlarm = (temperature > TEMP_THRESHOLD || gasValue > GAS_THRESHOLD) && !buzzerOverride;
  bool isMuted = (millis() < muteUntil);
  
  if (shouldAlarm && !alarmActive && !isMuted) {
    alarmActive = true;
    digitalWrite(BUZZER_PIN, HIGH);
    sendFireAlarm(temperature, gasValue);
  } else if (!shouldAlarm && alarmActive && !buzzerOverride) {
    alarmActive = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
  
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["gas"] = gasValue;
  doc["temperature"] = temperature;
  doc["humidity"] = humidityValue;
  doc["alarmActive"] = alarmActive;
  doc["buzzerOverride"] = buzzerOverride;
  doc["muted"] = isMuted;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("sensorData", jsonString);
  
  lastSensorUpdate = millis();
}

void sendFireAlarm(float temperature, int gasValue) {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["serial_number"] = SERIAL_NUMBER;
  doc["alarm_type"] = (temperature > TEMP_THRESHOLD) ? "fire" : "gas";
  doc["severity"] = "high";
  doc["temperature"] = temperature;
  doc["gas_level"] = gasValue;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("alarm_trigger", jsonString);
}

void sendCommandResponse(String command, bool success, String message) {
  JsonDocument doc;
  doc["success"] = success;
  doc["result"] = message;
  doc["deviceId"] = DEVICE_ID;
  doc["commandId"] = command;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("command_response", jsonString);
}

void sendDeviceStatus() {
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["alarmActive"] = alarmActive;
  doc["buzzerOverride"] = buzzerOverride;
  doc["muted"] = (millis() < muteUntil);
  doc["thresholds"]["gas"] = GAS_THRESHOLD;
  doc["thresholds"]["temperature"] = TEMP_THRESHOLD;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("deviceStatus", jsonString);
}

void triggerTestAlarm() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(3000);
  digitalWrite(BUZZER_PIN, LOW);
  sendFireAlarm(25.0, 100);
}

void muteAlarmForDuration(int seconds) {
  muteUntil = millis() + (seconds * 1000);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[MUTE] Alarm muted for " + String(seconds) + " seconds");
}

void setupWebSocketConnection() {
  snprintf(websocketPath, sizeof(websocketPath), WEBSOCKET_PATH_TEMPLATE, SERIAL_NUMBER);
  
  Serial.println("\n[WebSocket] Starting SSL connection...");
  Serial.printf("Host: %s:%d\n", WEBSOCKET_HOST, WEBSOCKET_PORT);
  Serial.printf("Path: %s\n", websocketPath);
  
  // SSL WebSocket connection
  webSocket.beginSSL(WEBSOCKET_HOST, WEBSOCKET_PORT, websocketPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  Serial.println("[WebSocket] SSL connection initiated");
}

/**************************************************************
 * MAIN FUNCTIONS
 **************************************************************/
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP8266 Fire Alarm v16.0 - UDP + SSL ===");
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("Serial Number: " + String(SERIAL_NUMBER));
  
  // Initialize hardware
  pinMode(MQ2_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUZZER_PIN_N, OUTPUT);
  digitalWrite(BUZZER_PIN_N, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize EEPROM and load configuration
  initEEPROM();
  loadConfiguration();
  
  // Load WiFi credentials
  wifiSSID = readEEPROMString(WIFI_SSID_ADDR, 100);
  wifiPassword = readEEPROMString(WIFI_PASS_ADDR, 100);
  
  // Initialize I2C and sensor
  Wire.begin(4, 5);
  if (!aht.begin()) {
    Serial.println("[WARNING] AHT sensor not found! Using simulated data");
    sensorAvailable = false;
  } else {
    Serial.println("[SUCCESS] AHT sensor initialized");
    sensorAvailable = true;
  }
  
  // Connect to WiFi or start hotspot
  if (wifiSSID.length() > 0 && connectToWiFi(wifiSSID, wifiPassword)) {
    setupWebSocketConnection();
  } else {
    startHotspot();
    udp.begin(UDP_PORT);
    Serial.printf("[UDP] Listening on port %d\n", UDP_PORT);
  }
  
  // Start HTTP server
  setupHttpServer();
  
  // Test buzzer at startup
  Serial.println("🔔 Testing buzzer at startup...");
  for(int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(500);
    digitalWrite(BUZZER_PIN, LOW);
    delay(500);
  }
  Serial.println("🔔 Buzzer test completed");
}

void loop() {
  if (!isHotspotMode) {
    webSocket.loop();
  }
  
  httpServer.handleClient();
  
  if (isHotspotMode) {
    handleUDP();
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Connection lost, reconnecting...");
      if (!connectToWiFi(wifiSSID, wifiPassword)) {
        startHotspot();
        udp.begin(UDP_PORT);
      }
      return;
    }
    
    if (isConnected && namespaceConnected) {
      sendSensorData();
      
      if (millis() - lastPingTime > PING_INTERVAL) {
        webSocket.sendTXT("2"); // Send ping
        lastPingTime = millis();
      }
    }
  }
  
  delay(100);
}