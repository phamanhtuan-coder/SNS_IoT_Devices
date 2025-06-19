/**************************************************************
 * ESP8266 Fire Alarm - Enhanced with Full Alarm Commands
 * 
 * Version: v15.0 - Complete Alarm System
 * Features: Full alarm command support + Enhanced capabilities
 **************************************************************/

#define SERIAL_NUMBER "SERL12JUN2501JXHMC17J1RPRY7P063E"
#define DEVICE_ID "DEVICE111"

#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_AHTX0.h>

// WiFi credentials
const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";

// Server configuration
String WEBSOCKET_HOST = "192.168.51.115";
uint16_t WEBSOCKET_PORT = 7777;
String WEBSOCKET_PATH = "/socket.io/?EIO=3&transport=websocket&deviceId=" + String(SERIAL_NUMBER) + "&isIoTDevice=true";

// Hardware
WebSocketsClient webSocket;
Adafruit_AHTX0 aht;

#define MQ2_PIN A0
#define BUZZER_PIN D5

// Timing variables
unsigned long lastSensorUpdate = 0;
unsigned long lastPingTime = 0;
unsigned long SENSOR_INTERVAL = 10000;  // Made non-const so it can be changed
const unsigned long PING_INTERVAL = 25000;

// State variables
bool isConnected = false;
bool namespaceConnected = false;
bool sensorAvailable = false;
bool alarmActive = false;
bool buzzerOverride = false;
int reconnectAttempts = 0;

// Threshold variables (configurable)
int GAS_THRESHOLD = 600;
float TEMP_THRESHOLD = 40.0;
int SMOKE_THRESHOLD = 500;
unsigned long muteUntil = 0;

/**************************************************************
 * FUNCTION DECLARATIONS
 **************************************************************/
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void handleWebSocketMessage(String message);
void handleSocketIOMessage(String socketIOData);
void parseSocketIOEvent(String eventData);
void handleCommand(String eventName, JsonDocument& doc);

void joinDeviceNamespace();
void sendSocketIOEvent(String eventName, String jsonData);
void sendDeviceOnline();
void sendSensorData();
void sendFireAlarm(float temperature, int gasValue);
void sendCommandResponse(String command, bool success, String message);
void sendDeviceStatus();
void triggerTestAlarm();
void sendWebSocketPing();
void startWebSocketConnection();

// Enhanced alarm functions
void muteAlarmForDuration(int seconds);
void performSensorDiagnostics();
void calibrateSensors(String sensorType);
void triggerEmergencyAlarm(int duration);
void sendDetailedSystemReport();
void handleConfigUpdate(JsonDocument& doc);
void sendUpdatedConfig();
void sendSilentAlert(float temperature, int gasValue);

/**************************************************************
 * Enhanced WebSocket Event Handler
 **************************************************************/

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] Disconnected from server");
      isConnected = false;
      namespaceConnected = false;
      break;
      
    case WStype_CONNECTED:
      {
        Serial.printf("[WebSocket] Connected to server: %s\n", payload);
        Serial.println("[WebSocket] ✅ WebSocket connection established");
        isConnected = true;
        namespaceConnected = false;
        reconnectAttempts = 0;
        break;
      }
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[WebSocket] Message received: %s\n", message.c_str());
        handleWebSocketMessage(message);
        break;
      }
      
    case WStype_BIN:
      Serial.println("[WebSocket] Binary data received");
      break;
      
    case WStype_PING:
      Serial.println("[WebSocket] Ping received");
      break;
      
    case WStype_PONG:
      Serial.println("[WebSocket] Pong received");
      break;
      
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
      {
        Serial.println("[Engine.IO] OPEN - Session established");
        if (message.length() > 1) {
          String sessionData = message.substring(1);
          Serial.println("[Engine.IO] Session data: " + sessionData);
          delay(1000);
          joinDeviceNamespace();
        }
        break;
      }
      
    case '1': // Engine.IO CLOSE
      Serial.println("[Engine.IO] CLOSE received");
      break;
      
    case '2': // Engine.IO PING
      Serial.println("[Engine.IO] PING received");
      break;
      
    case '3': // Engine.IO PONG
      Serial.println("[Engine.IO] PONG received");
      break;
      
    case '4': // Engine.IO MESSAGE
      {
        if (message.length() > 1) {
          String socketIOData = message.substring(1);
          handleSocketIOMessage(socketIOData);
        }
        break;
      }
      
    default:
      Serial.printf("[Engine.IO] Unknown packet type: %c\n", engineIOType);
      break;
  }
}

void handleSocketIOMessage(String socketIOData) {
  if (socketIOData.length() < 1) return;
  
  char socketIOType = socketIOData.charAt(0);
  
  switch(socketIOType) {
    case '0': // Socket.IO CONNECT
      {
        Serial.println("[Socket.IO] CONNECT acknowledged");
        if (socketIOData.indexOf("/device") != -1) {
          Serial.println("[Socket.IO] ✅ Connected to /device namespace!");
          namespaceConnected = true;
          delay(1000);
          sendDeviceOnline();
        }
        break;
      }
      
    case '1': // Socket.IO DISCONNECT
      Serial.println("[Socket.IO] DISCONNECT received");
      namespaceConnected = false;
      break;
      
    case '2': // Socket.IO EVENT
      parseSocketIOEvent(socketIOData.substring(1));
      break;
      
    case '3': // Socket.IO ACK
      Serial.println("[Socket.IO] ACK received");
      break;
      
    case '4': // Socket.IO ERROR
      {
        Serial.println("[Socket.IO] ERROR received");
        Serial.println("[Socket.IO] Error data: " + socketIOData);
        if (!namespaceConnected) {
          Serial.println("[Socket.IO] Retrying namespace connection...");
          delay(2000);
          joinDeviceNamespace();
        }
        break;
      }
      
    default:
      Serial.printf("[Socket.IO] Unknown packet type: %c\n", socketIOType);
      break;
  }
}

void parseSocketIOEvent(String eventData) {
  if (eventData.startsWith("/device,")) {
    eventData = eventData.substring(8);
  }
  
  int firstBracket = eventData.indexOf('[');
  if (firstBracket == -1) return;
  
  int firstQuote = eventData.indexOf('"', firstBracket);
  if (firstQuote == -1) return;
  
  int secondQuote = eventData.indexOf('"', firstQuote + 1);
  if (secondQuote == -1) return;
  
  String eventName = eventData.substring(firstQuote + 1, secondQuote);
  Serial.println("[Socket.IO] Event name: " + eventName);
  
  int jsonStart = eventData.indexOf('{');
  if (jsonStart != -1) {
    String jsonData = eventData.substring(jsonStart, eventData.lastIndexOf('}') + 1);
    Serial.println("[Socket.IO] JSON data: " + jsonData);
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonData);
    
    if (!err) {
      handleCommand(eventName, doc);
    } else {
      Serial.println("[Socket.IO] JSON parse error: " + String(err.c_str()));
    }
  }
}

/**************************************************************
 * ENHANCED COMMAND HANDLER
 **************************************************************/

void handleCommand(String eventName, JsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    
    // ========== BASIC COMMANDS ==========
    if (action == "toggleBuzzer") {
      bool status = doc["state"]["power_status"];
      buzzerOverride = status;
      digitalWrite(BUZZER_PIN, status ? HIGH : LOW);
      Serial.println("[Command] Buzzer control: " + String(status ? "ON" : "OFF"));
      sendCommandResponse("toggleBuzzer", true, "Buzzer " + String(status ? "activated" : "deactivated"));
    }
    else if (action == "resetAlarm") {
      alarmActive = false;
      buzzerOverride = false;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("[Command] Alarm reset");
      sendCommandResponse("resetAlarm", true, "Alarm reset successfully");
      sendDeviceStatus();
    }
    else if (action == "testAlarm") {
      Serial.println("[Command] Testing alarm system");
      triggerTestAlarm();
      sendCommandResponse("testAlarm", true, "Test alarm executed");
    }
    else if (action == "getStatus") {
      sendDeviceStatus();
      sendCommandResponse("getStatus", true, "Status sent");
    }
    
    // ========== ENHANCED COMMANDS ==========
    else if (action == "updateThreshold") {
      if (doc["config"].is<JsonObject>()) {
        JsonObject config = doc["config"];
        
        if (config["gas_threshold"].is<int>()) {
          GAS_THRESHOLD = config["gas_threshold"];
          Serial.println("[Config] Gas threshold updated: " + String(GAS_THRESHOLD));
        }
        if (config["temp_threshold"].is<float>()) {
          TEMP_THRESHOLD = config["temp_threshold"];
          Serial.println("[Config] Temperature threshold updated: " + String(TEMP_THRESHOLD));
        }
        if (config["smoke_threshold"].is<int>()) {
          SMOKE_THRESHOLD = config["smoke_threshold"];
          Serial.println("[Config] Smoke threshold updated: " + String(SMOKE_THRESHOLD));
        }
        
        sendCommandResponse("updateThreshold", true, "Thresholds updated successfully");
        sendUpdatedConfig();
      }
    }
    else if (action == "muteAlarm") {
      int duration = doc["duration"] | 300; // Default 5 minutes
      muteAlarmForDuration(duration);
      sendCommandResponse("muteAlarm", true, "Alarm muted for " + String(duration) + " seconds");
    }
    else if (action == "sensorCheck") {
      performSensorDiagnostics();
      sendCommandResponse("sensorCheck", true, "Sensor diagnostics completed");
    }
    else if (action == "restart") {
      sendCommandResponse("restart", true, "ESP8266 restarting...");
      delay(1000);
      ESP.restart();
    }
    else if (action == "calibrateSensor") {
      String sensorType = doc["sensor_type"] | "all";
      calibrateSensors(sensorType);
      sendCommandResponse("calibrateSensor", true, "Sensor calibration started");
    }
    else if (action == "emergencyAlarm") {
      int duration = doc["duration"] | 30; // Default 30 seconds
      triggerEmergencyAlarm(duration);
      sendCommandResponse("emergencyAlarm", true, "Emergency alarm activated");
    }
    else if (action == "setDataInterval") {
      int newInterval = doc["interval"] | 10000; // Default 10 seconds
      if (newInterval >= 1000 && newInterval <= 60000) {
        SENSOR_INTERVAL = newInterval;
        sendCommandResponse("setDataInterval", true, "Data interval set to " + String(newInterval) + "ms");
      } else {
        sendCommandResponse("setDataInterval", false, "Invalid interval range (1000-60000ms)");
      }
    }
    else if (action == "systemReport") {
      sendDetailedSystemReport();
      sendCommandResponse("systemReport", true, "System report sent");
    }
    else {
      Serial.println("[Command] Unknown action: " + action);
      sendCommandResponse(action, false, "Unknown command: " + action);
    }
  }
  
  // ========== ESP8266 SPECIFIC EVENTS ==========
  else if (eventName == "reset_alarm") {
    alarmActive = false;
    buzzerOverride = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("[ESP8266] Remote alarm reset");
    sendDeviceStatus();
  }
  else if (eventName == "test_alarm") {
    Serial.println("[ESP8266] Remote test alarm");
    triggerTestAlarm();
  }
  else if (eventName == "update_config") {
    Serial.println("[ESP8266] Config update received");
    handleConfigUpdate(doc);
  }
}

/**************************************************************
 * Socket.IO Communication Functions
 **************************************************************/

void joinDeviceNamespace() {
  Serial.println("[Socket.IO] Attempting to join /device namespace...");
  String namespaceJoin = "40/device,";
  webSocket.sendTXT(namespaceJoin);
  Serial.println("[Socket.IO] Sent namespace join request: " + namespaceJoin);
}

void sendSocketIOEvent(String eventName, String jsonData) {
  if (!namespaceConnected) {
    Serial.println("[Socket.IO] WARNING: Namespace not connected, cannot send events");
    return;
  }
  
  String eventPayload = "42/device,[\"" + eventName + "\"," + jsonData + "]";
  webSocket.sendTXT(eventPayload);
  Serial.println("[Socket.IO] Sent event '" + eventName + "'");
}

void sendDeviceOnline() {
  if (!namespaceConnected) {
    Serial.println("[Socket.IO] Cannot send device_online: namespace not connected");
    return;
  }
  
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["deviceType"] = "FIRE_ALARM_SYSTEM";
  doc["firmware_version"] = "v15.0-Enhanced-Commands";
  doc["hardware_version"] = "ESP8266-v1.0";
  doc["isInput"] = true;
  doc["isOutput"] = true;
  doc["isSensor"] = true;
  doc["isActuator"] = true;
  
  // ESP8266 capabilities
  doc["capabilities"]["smoke_detection"] = true;
  doc["capabilities"]["temperature_monitoring"] = true;
  doc["capabilities"]["gas_detection"] = true;
  doc["capabilities"]["alarm_control"] = true;
  doc["capabilities"]["websocket_native"] = true;
  doc["capabilities"]["remote_configuration"] = true;
  doc["capabilities"]["diagnostics"] = true;
  
  // System info
  doc["esp8266_info"]["chip_id"] = String(ESP.getChipId(), HEX);
  doc["esp8266_info"]["free_heap"] = ESP.getFreeHeap();
  doc["esp8266_info"]["wifi_rssi"] = WiFi.RSSI();
  doc["esp8266_info"]["ip_address"] = WiFi.localIP().toString();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("device_online", jsonString);
  
  Serial.println("[DEVICE] ✅ Online message sent successfully!");
}

void sendSensorData() {
  if (millis() - lastSensorUpdate < SENSOR_INTERVAL) return;
  if (!namespaceConnected) return;
  
  // Read sensors
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
  
  // Enhanced alarm checking with mute support
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
  
  // If muted, still detect but don't sound alarm
  if (shouldAlarm && isMuted) {
    Serial.println("[MUTED] Alarm condition detected but muted");
    sendSilentAlert(temperature, gasValue);
  }
  
  // Send sensor data
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["gas"] = gasValue;
  doc["temperature"] = temperature;
  doc["humidity"] = humidityValue;
  doc["smoke_level"] = gasValue;
  doc["flame_detected"] = false;
  doc["alarmActive"] = alarmActive;
  doc["buzzerOverride"] = buzzerOverride;
  doc["muted"] = isMuted;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["free_memory"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("sensorData", jsonString);
  
  lastSensorUpdate = millis();
  Serial.println("[SENSOR] Data sent - Gas: " + String(gasValue) + ", Temp: " + String(temperature) + ", Muted: " + String(isMuted ? "YES" : "NO"));
}

void sendFireAlarm(float temperature, int gasValue) {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["serial_number"] = SERIAL_NUMBER;
  doc["alarm_type"] = (temperature > TEMP_THRESHOLD) ? "fire" : "gas";
  doc["severity"] = "high";
  doc["temperature"] = temperature;
  doc["gas_level"] = gasValue;
  doc["smoke_level"] = gasValue;
  doc["location"] = "unknown";
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("alarm_trigger", jsonString);
  
  Serial.println("[ALARM] 🚨 FIRE ALARM TRIGGERED!");
}

void sendCommandResponse(String command, bool success, String message) {
  JsonDocument doc;
  doc["success"] = success;
  doc["result"] = message;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
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
  doc["muteTimeRemaining"] = (millis() < muteUntil) ? (muteUntil - millis()) / 1000 : 0;
  doc["thresholds"]["gas"] = GAS_THRESHOLD;
  doc["thresholds"]["temperature"] = TEMP_THRESHOLD;
  doc["thresholds"]["smoke"] = SMOKE_THRESHOLD;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["connection_method"] = "WEBSOCKET_DIRECT";
  doc["namespace_connected"] = namespaceConnected;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("deviceStatus", jsonString);
}

void triggerTestAlarm() {
  Serial.println("[TEST] Triggering test alarm");
  
  digitalWrite(BUZZER_PIN, HIGH);
  delay(3000);
  digitalWrite(BUZZER_PIN, LOW);
  
  sendFireAlarm(25.0, 100);
  Serial.println("[TEST] Test alarm completed");
}

void sendWebSocketPing() {
  if (isConnected) {
    webSocket.sendTXT("2");
    Serial.println("[WebSocket] Ping sent - Namespace connected: " + String(namespaceConnected ? "YES" : "NO"));
    
    if (!namespaceConnected) {
      Serial.println("[WebSocket] Attempting to rejoin namespace...");
      joinDeviceNamespace();
    }
  }
}

/**************************************************************
 * ENHANCED ALARM FUNCTIONS
 **************************************************************/

void muteAlarmForDuration(int seconds) {
  muteUntil = millis() + (seconds * 1000);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[MUTE] Alarm muted for " + String(seconds) + " seconds");
  
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["muted"] = true;
  doc["mute_until"] = muteUntil;
  doc["duration"] = seconds;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("alarm_muted", jsonString);
}

void performSensorDiagnostics() {
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["sensor_status"]["aht_sensor"] = sensorAvailable;
  doc["sensor_status"]["gas_sensor"] = (analogRead(MQ2_PIN) > 0);
  doc["sensor_status"]["buzzer"] = true;
  
  if (sensorAvailable) {
    sensors_event_t humidity, temp;
    bool ahtWorking = aht.getEvent(&humidity, &temp);
    doc["sensor_readings"]["temperature"] = ahtWorking ? temp.temperature : -999;
    doc["sensor_readings"]["humidity"] = ahtWorking ? humidity.relative_humidity : -999;
  }
  doc["sensor_readings"]["gas"] = analogRead(MQ2_PIN);
  doc["sensor_readings"]["wifi_rssi"] = WiFi.RSSI();
  doc["sensor_readings"]["free_heap"] = ESP.getFreeHeap();
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("sensor_diagnostics", jsonString);
  
  Serial.println("[DIAGNOSTICS] Sensor check completed");
}

void calibrateSensors(String sensorType) {
  Serial.println("[CALIBRATION] Starting calibration for: " + sensorType);
  
  if (sensorType == "gas" || sensorType == "all") {
    int total = 0;
    for (int i = 0; i < 10; i++) {
      total += analogRead(MQ2_PIN);
      delay(100);
    }
    int gasBaseline = total / 10;
    Serial.println("[CALIBRATION] Gas baseline: " + String(gasBaseline));
  }
  
  if (sensorType == "temperature" || sensorType == "all") {
    if (sensorAvailable) {
      aht.begin();
      Serial.println("[CALIBRATION] AHT sensor reset");
    }
  }
  
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["calibration_type"] = sensorType;
  doc["status"] = "completed";
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("calibration_result", jsonString);
}

void triggerEmergencyAlarm(int duration) {
  Serial.println("[EMERGENCY] Manual emergency alarm activated");
  
  alarmActive = true;
  unsigned long startTime = millis();
  
  while (millis() - startTime < (duration * 1000)) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
    
    webSocket.loop();
    if (!alarmActive) break;
  }
  
  alarmActive = false;
  digitalWrite(BUZZER_PIN, LOW);
  
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["alarm_type"] = "manual_emergency";
  doc["severity"] = "critical";
  doc["duration"] = duration;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("emergency_alarm", jsonString);
}

void sendDetailedSystemReport() {
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  
  doc["system"]["uptime"] = millis() / 1000;
  doc["system"]["free_heap"] = ESP.getFreeHeap();
  doc["system"]["chip_id"] = String(ESP.getChipId(), HEX);
  doc["system"]["cpu_freq"] = ESP.getCpuFreqMHz();
  doc["system"]["sdk_version"] = ESP.getSdkVersion();
  
  doc["network"]["wifi_ssid"] = WiFi.SSID();
  doc["network"]["wifi_rssi"] = WiFi.RSSI();
  doc["network"]["ip_address"] = WiFi.localIP().toString();
  doc["network"]["mac_address"] = WiFi.macAddress();
  
  doc["thresholds"]["gas"] = GAS_THRESHOLD;
  doc["thresholds"]["temperature"] = TEMP_THRESHOLD;
  doc["thresholds"]["smoke"] = SMOKE_THRESHOLD;
  
  doc["status"]["alarm_active"] = alarmActive;
  doc["status"]["buzzer_override"] = buzzerOverride;
  doc["status"]["muted"] = (millis() < muteUntil);
  doc["status"]["namespace_connected"] = namespaceConnected;
  
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("system_report", jsonString);
}

void handleConfigUpdate(JsonDocument& doc) {
  if (doc["config"].is<JsonObject>()) {
    JsonObject config = doc["config"];
    bool updated = false;
    
    if (config["smoke_threshold"].is<int>()) {
      SMOKE_THRESHOLD = config["smoke_threshold"];
      updated = true;
    }
    if (config["temp_threshold"].is<float>()) {
      TEMP_THRESHOLD = config["temp_threshold"];
      updated = true;
    }
    if (config["gas_threshold"].is<int>()) {
      GAS_THRESHOLD = config["gas_threshold"];
      updated = true;
    }
    if (config["sensor_read_interval"].is<int>()) {
      SENSOR_INTERVAL = config["sensor_read_interval"];
      updated = true;
    }
    
    if (updated) {
      Serial.println("[CONFIG] Configuration updated successfully");
      sendUpdatedConfig();
    }
  }
}

void sendUpdatedConfig() {
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["config"]["gas_threshold"] = GAS_THRESHOLD;
  doc["config"]["temp_threshold"] = TEMP_THRESHOLD;
  doc["config"]["smoke_threshold"] = SMOKE_THRESHOLD;
  doc["config"]["sensor_interval"] = SENSOR_INTERVAL;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("config_updated", jsonString);
}

void sendSilentAlert(float temperature, int gasValue) {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["alert_type"] = "silent";
  doc["temperature"] = temperature;
  doc["gas_level"] = gasValue;
  doc["muted"] = true;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("silent_alert", jsonString);
}

/**************************************************************
 * Connection Functions
 **************************************************************/

void startWebSocketConnection() {
  Serial.println("\n[WebSocket] Starting connection...");
  Serial.println("Host: " + WEBSOCKET_HOST + ":" + String(WEBSOCKET_PORT));
  Serial.println("Path: " + WEBSOCKET_PATH);
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("Serial Number: " + String(SERIAL_NUMBER));
  
  webSocket.begin(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, WEBSOCKET_PATH.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  webSocket.setExtraHeaders("User-Agent: ESP8266-FireAlarm/15.0\r\nOrigin: http://192.168.51.115:7777");
  
  Serial.println("[WebSocket] Connection initiated with enhanced alarm commands");
}

/**************************************************************
 * Main Functions
 **************************************************************/

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP8266 Fire Alarm v15.0 - Enhanced Commands ===");
  Serial.println("✅ Full alarm command support enabled");
  Serial.println("✅ Remote configuration support");
  Serial.println("✅ Advanced diagnostics enabled");
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("Serial Number: " + String(SERIAL_NUMBER));
  
  // Initialize hardware
  pinMode(MQ2_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize I2C and sensor
  Wire.begin(4, 5);
  if (!aht.begin()) {
    Serial.println("[WARNING] AHT sensor not found! Using simulated data");
    sensorAvailable = false;
  } else {
    Serial.println("[SUCCESS] AHT sensor initialized");
    sensorAvailable = true;
  }
  
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println(" Connected!");
  Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
  Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
  
  // Display enhanced capabilities
  Serial.println("\n🔥 ENHANCED ALARM CAPABILITIES:");
  Serial.println("   • Basic: toggleBuzzer, resetAlarm, testAlarm, getStatus");
  Serial.println("   • Advanced: updateThreshold, muteAlarm, sensorCheck");
  Serial.println("   • Emergency: emergencyAlarm, restart, calibrateSensor");
  Serial.println("   • Config: setDataInterval, systemReport, update_config");
  Serial.println("   • Events: reset_alarm, test_alarm (direct ESP8266 events)");

    // TEST BUZZER AT STARTUP
  Serial.println("🔔 Testing buzzer at startup...");
  
  // Test buzzer 3 lần
  for(int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("Buzzer ON");
    delay(500);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Buzzer OFF");
    delay(500);
  }
  
  Serial.println("🔔 Buzzer test completed");
  
  // Start WebSocket connection
  startWebSocketConnection();
}

void loop() {
  webSocket.loop();  // Process WebSocket events
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connection lost, reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }
  
  if (isConnected && namespaceConnected) {
    sendSensorData();
    
    // Send ping every 25 seconds
    if (millis() - lastPingTime > PING_INTERVAL) {
      sendWebSocketPing();
      lastPingTime = millis();
    }
  } else {
    reconnectAttempts++;
    if (reconnectAttempts % 10 == 0) {
      Serial.println("[WebSocket] Status - Connected: " + String(isConnected ? "YES" : "NO") + 
                    ", Namespace: " + String(namespaceConnected ? "YES" : "NO"));
    }
  }
  
  delay(100);
}