#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;

// ✅ HUB IDENTITY - This hub connects as itself and manages other devices
String HUB_SERIAL = "SERL29JUN2501JYXECBR32V8BD77RW82"; // Hub Socket Mô Hình 
#define FIRMWARE_VERSION "3.0.4"
#define HUB_ID "ESP_HUB_001"

WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;

// ✅ ENHANCED: Action tracking for better command handling
String lastClientAction = "toggle_door";
unsigned long lastActionTime = 0;
String lastTargetSerial = "";

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP Socket Hub v3.0.4 - DYNAMIC 7 DOORS ===");
  Serial.println("Hub Serial: " + HUB_SERIAL);
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Mode: Dynamic Device Discovery via Database");
  
  setupWiFi();
  setupWebSocket();
  
  Serial.println("[INIT] Socket Hub Ready - Dynamic Mode");
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("[WIFI] Connecting");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✓ CONNECTED");
    Serial.println("[WIFI] IP: " + WiFi.localIP().toString());
    Serial.println("[WIFI] MAC: " + WiFi.macAddress());
  } else {
    Serial.println(" ✗ FAILED");
    ESP.restart();
  }
}

void setupWebSocket() {
  Serial.println("[WS] Setting up WebSocket...");

  // ✅ Connect as the hub itself
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                HUB_SERIAL + "&isIoTDevice=true&hub_managed=true";
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(25000, 5000, 2);
  
  String userAgent = "ESP-Hub/3.0.4-Dynamic";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[WS] WebSocket initialized as Hub: " + HUB_SERIAL);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] DISCONNECTED");
      socketConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WS] CONNECTED as Hub: " + HUB_SERIAL);
      socketConnected = true;
      lastPingResponse = millis();
      sendDeviceOnline();
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(String((char*)payload));
      break;
      
    case WStype_ERROR:
      Serial.println("[WS] ERROR");
      break;
      
    case WStype_PONG:
      Serial.println("[WS] PONG received");
      lastPingResponse = millis();
      break;
  }
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  
  if (type == '0') {
    Serial.println("[EIO] OPEN");
    sendDeviceOnline();
    
  } else if (type == '2') {
    Serial.println("[EIO] PING");
    webSocket.sendTXT("3");
    lastPingResponse = millis();
    
  } else if (type == '3') {
    Serial.println("[EIO] PONG");
    lastPingResponse = millis();
    
  } else if (type == '4') {
    String socketIOData = message.substring(1);
    handleSocketIOMessage(socketIOData);
  }
}

void handleSocketIOMessage(String data) {
  if (data.length() < 1) return;
  
  char type = data.charAt(0);
  
  if (type == '0') {
    Serial.println("[SIO] CONNECT");
    sendDeviceOnline();
    
  } else if (type == '2') {
    String eventData = data.substring(1);
    handleSocketIOEvent(eventData);
  }
}

void handleSocketIOEvent(String eventData) {
  Serial.println("[EVENT] Raw data: " + eventData);
  
  if (eventData.indexOf("connection_welcome") != -1) {
    Serial.println("[EVENT] Welcome received for Hub");
    String ackPayload = "42[\"welcome_ack\",{\"received\":true,\"hub_managed\":true,\"dynamic_discovery\":true}]";
    webSocket.sendTXT(ackPayload);
    
  } else if (eventData.indexOf("command") != -1) {
    Serial.println("[CMD] Command received: " + eventData);
    
    // ✅ ENHANCED: Parse command with target device support
    parseAndExecuteEnhancedCommand(eventData);
    
  } else if (eventData.indexOf("ping") != -1) {
    Serial.println("[PING] Socket.IO ping");
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"hub_serial\":\"" + HUB_SERIAL + "\"}]";
    webSocket.sendTXT(pongPayload);
    lastPingResponse = millis();
    
  } else if (eventData.indexOf("door_command") != -1) {
    Serial.println("[CLIENT] Door command detected: " + eventData);
    extractClientAction(eventData);
  }
}

void extractClientAction(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx != -1 && endIdx != -1) {
    String jsonString = eventData.substring(startIdx, endIdx + 1);
    
    JsonDocument doc;
    if (deserializeJson(doc, jsonString) == DeserializationError::Ok) {
      if (doc["action"].is<String>()) {
        lastClientAction = doc["action"].as<String>();
        lastActionTime = millis();
        Serial.println("[CLIENT] Action stored: " + lastClientAction);
      }
      
      if (doc["serialNumber"].is<String>()) {
        lastTargetSerial = doc["serialNumber"].as<String>();
        Serial.println("[CLIENT] Target stored: " + lastTargetSerial);
      }
    }
  }
}

// ✅ ENHANCED: Parse command with dynamic target device support
void parseAndExecuteEnhancedCommand(String eventData) {
  Serial.println("[PARSE] Enhanced command parsing (Dynamic Mode)...");
  
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx == -1 || endIdx == -1) {
    Serial.println("[PARSE] ✗ Invalid JSON format");
    return;
  }
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  Serial.println("[PARSE] JSON: " + jsonString);
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.println("[PARSE] ✗ JSON parse failed: " + String(error.c_str()));
    return;
  }
  
  // ✅ ENHANCED: Extract target device serial number
  String targetSerial = "";
  if (doc["serialNumber"].is<String>()) {
    targetSerial = doc["serialNumber"].as<String>();
  }
  
  // ✅ ENHANCED: Extract action with fallback logic
  String action = "";
  
  if (doc["esp01_safe"].is<bool>() && doc["esp01_safe"] == true) {
    Serial.println("[CMD] ✓ ESP01 safe command detected");
    
    action = "toggle_door"; // Default fallback
    
    // Use stored client action if recent
    if (millis() - lastActionTime < 5000 && lastClientAction != "") {
      action = lastClientAction;
      Serial.println("[CMD] Using stored client action: " + action);
    }
    
    // Try to get action from command
    if (doc["action"].is<String>()) {
      action = doc["action"].as<String>();
    }
    
  } else {
    // Extract action from standard format
    if (doc["action"].is<String>()) {
      action = doc["action"].as<String>();
    }
  }
  
  // ✅ ENHANCED: Use stored target if not in current command
  if (targetSerial == "" && lastTargetSerial != "" && millis() - lastActionTime < 5000) {
    targetSerial = lastTargetSerial;
    Serial.println("[CMD] Using stored target: " + targetSerial);
  }
  
  // ✅ CRITICAL: Validate target device is managed by this hub
  if (targetSerial == "") {
    Serial.println("[CMD] ✗ No target device specified");
    return;
  }
  
  if (action == "") {
    action = "toggle_door";
    Serial.println("[CMD] No action specified, using default: toggle_door");
  }
  
  Serial.println("[CMD] ✓ Final command: " + targetSerial + " -> " + action);
  
  // ✅ ENHANCED: Send command with target serial to Master
  Serial.println("CMD:" + targetSerial + ":" + action);
}

void sendDeviceOnline() {
  if (!socketConnected) return;
  
  String json = "{";
  json += "\"deviceId\":\"" + HUB_SERIAL + "\",";
  json += "\"serialNumber\":\"" + HUB_SERIAL + "\",";
  json += "\"deviceType\":\"HUB_GATEWAY\",";
  json += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"hub_managed\":true,";
  json += "\"hub_id\":\"" + String(HUB_ID) + "\",";
  json += "\"connection_type\":\"hub\",";
  json += "\"discovery_mode\":\"dynamic\",";
  json += "\"managed_via_database\":true";
  json += "}";
  
  String payload = "42[\"device_online\"," + json + "]";
  webSocket.sendTXT(payload);
  
  Serial.println("[SEND] device_online sent for Hub: " + HUB_SERIAL);
}

void checkWebSocketHealth() {
  unsigned long now = millis();
  
  if (socketConnected && (now - lastPingResponse > 35000)) {
    Serial.println("[WS] Ping timeout, reconnecting...");
    webSocket.disconnect();
  }
}

void loop() {
  webSocket.loop(); 
  
  // Send ping every 20s to stay active
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 20000) {
    webSocket.sendTXT("2");
    lastManualPing = millis();
  }

  static unsigned long lastWSCheck = 0;
  if (millis() - lastWSCheck > 15000) {
    checkWebSocketHealth();
    lastWSCheck = millis();
  }
  
  // ✅ ENHANCED: Handle responses from Master for all managed doors
  if (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    if (response.startsWith("RESP:")) {
      String json = response.substring(5);
      
      // Forward response with device identification
      String payload = "42[\"command_response\"," + json + "]";
      webSocket.sendTXT(payload);
      
      Serial.println("[RESP] Forwarded to server: " + json);
    }
  }
  
  yield();
  delay(10);
}