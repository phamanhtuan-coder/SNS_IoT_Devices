#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;  // ✅ Thay đổi từ 7777 sang 443 cho HTTPS
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVVX08V40YMGTW";
#define FIRMWARE_VERSION "3.0.3"
#define HUB_ID "ESP_HUB_001"

WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;

// ✅ FIX: Add missing global variables
String lastClientAction = "toggle_door";
unsigned long lastActionTime = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP Socket Hub v3.0.3 FIXED ===");
  Serial.println("Hub ID: " + String(HUB_ID));
  
  setupWiFi();
  setupWebSocket();
  
  Serial.println("[INIT] Socket Hub Ready");
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


  
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                DEVICE_SERIAL + "&isIoTDevice=true&hub_managed=true";
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(25000, 5000, 2);  // 25s ping, 5s timeout, 2 retries
  
  String userAgent = "ESP-Hub/3.0.3";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[WS] WebSocket initialized for: " + DEVICE_SERIAL);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] DISCONNECTED");
      socketConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WS] CONNECTED");
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
    Serial.println("[EVENT] Welcome received");
    String ackPayload = "42[\"welcome_ack\",{\"received\":true,\"hub_managed\":true}]";
    webSocket.sendTXT(ackPayload);
    
  } else if (eventData.indexOf("command") != -1) {
    Serial.println("[CMD] Command received: " + eventData);
    
    // ✅ QUICK FIX: Check for action field in JSON
    if (eventData.indexOf("\"action\"") != -1) {
      // Extract action from enhanced command format
      if (eventData.indexOf("open_door") != -1) {
        Serial.println("[CMD] ✓ Action: open_door");
        Serial.println("CMD:open_door");
      } else if (eventData.indexOf("close_door") != -1) {
        Serial.println("[CMD] ✓ Action: close_door"); 
        Serial.println("CMD:close_door");
      } else if (eventData.indexOf("toggle_door") != -1) {
        Serial.println("[CMD] ✓ Action: toggle_door");
        Serial.println("CMD:toggle_door");
      } else {
        Serial.println("[CMD] ✓ Default action: toggle_door");
        Serial.println("CMD:toggle_door");
      }
    } else {
      // Fallback for old format
      parseAndExecuteCommand(eventData);
    }
    
  } else if (eventData.indexOf("ping") != -1) {
    Serial.println("[PING] Socket.IO ping");
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + "}]";
    webSocket.sendTXT(pongPayload);
    lastPingResponse = millis();
    
  } else if (eventData.indexOf("door_command") != -1) {
    // Listen for original client command to extract action
    Serial.println("[CLIENT] Door command detected: " + eventData);
    extractClientAction(eventData);
  }
}

void extractClientAction(String eventData) {
  // Extract action from client command for later use
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx != -1 && endIdx != -1) {
    String jsonString = eventData.substring(startIdx, endIdx + 1);
    
    // ✅ FIX: Use JsonDocument instead of DynamicJsonDocument
    JsonDocument doc;
    if (deserializeJson(doc, jsonString) == DeserializationError::Ok) {
      // ✅ FIX: Use modern ArduinoJson syntax
      if (doc["action"].is<String>()) {
        lastClientAction = doc["action"].as<String>();
        lastActionTime = millis();
        Serial.println("[CLIENT] Action stored: " + lastClientAction);
      }
    }
  }
}

void parseAndExecuteCommand(String eventData) {
  // Parse JSON command properly
  Serial.println("[PARSE] Parsing command...");
  Serial.println("[PARSE] Full event: " + eventData);
  
  // Extract JSON from Socket.IO format: ["command",{...}]
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx == -1 || endIdx == -1) {
    Serial.println("[PARSE] ✗ Invalid JSON format");
    return;
  }
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  Serial.println("[PARSE] JSON: " + jsonString);
  
  // ✅ FIX: Use JsonDocument instead of DynamicJsonDocument
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.println("[PARSE] ✗ JSON parse failed: " + String(error.c_str()));
    return;
  }
  
  // ✅ FIX: Check if this is esp01_safe command (server strips action)
  if (doc["esp01_safe"].is<bool>() && doc["esp01_safe"] == true) {
    Serial.println("[CMD] ✓ ESP01 safe command detected");
    
    String action = "toggle_door"; // Default fallback
    
    // Use stored client action if recent (within 5 seconds)
    if (millis() - lastActionTime < 5000 && lastClientAction != "") {
      action = lastClientAction;
      Serial.println("[CMD] Using stored client action: " + action);
    }
    
    // ✅ FIX: Try to get action from a hidden field
    if (doc["action"].is<String>()) {
      action = doc["action"].as<String>();
    } else if (doc["command_type"].is<String>()) {
      action = doc["command_type"].as<String>();
    }
    
    Serial.println("[CMD] ✓ Final action: " + action);
    Serial.println("CMD:" + action); // Send to Master via Serial
    return;
  }
  
  // Extract action from standard format
  String action = "";
  if (doc["action"].is<String>()) {
    action = doc["action"].as<String>();
  }
  
  if (action != "") {
    Serial.println("[CMD] ✓ Action extracted: " + action);
    Serial.println("CMD:" + action); // Send to Master via Serial
  } else {
    Serial.println("[CMD] ✗ No action found in command");
    
    // ✅ FIX: Debug using modern ArduinoJson
    Serial.println("[DEBUG] Available keys:");
    for (JsonPair kv : doc.as<JsonObject>()) {
      Serial.println("  " + String(kv.key().c_str()) + ": " + String(kv.value().as<String>()));
    }
    
    // Fallback: use stored action or default
    String fallbackAction = (lastClientAction != "") ? lastClientAction : "toggle_door";
    Serial.println("[FALLBACK] Using: " + fallbackAction);
    Serial.println("CMD:" + fallbackAction);
  }
}

void sendDeviceOnline() {
  if (!socketConnected) return;
  
  String json = "{";
  json += "\"deviceId\":\"" + DEVICE_SERIAL + "\",";
  json += "\"serialNumber\":\"" + DEVICE_SERIAL + "\",";
  json += "\"deviceType\":\"HUB\",";
  json += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"hub_managed\":true,";
  json += "\"hub_id\":\"" + String(HUB_ID) + "\",";
  json += "\"connection_type\":\"hub\"";
  json += "}";
  
  String payload = "42[\"device_online\"," + json + "]";
  webSocket.sendTXT(payload);
  
  Serial.println("[SEND] device_online sent");
}

void checkWebSocketHealth() {
  unsigned long now = millis();
  
  // ✅ Match server timeout  
  if (socketConnected && (now - lastPingResponse > 35000)) {  // 35s threshold
    Serial.println("[WS] Ping timeout, reconnecting...");
    webSocket.disconnect();
  }
}

void loop() {
  webSocket.loop(); 
  
  // ✅ Send ping every 20s to stay active
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 20000) {
    webSocket.sendTXT("2");  // Engine.IO ping
    lastManualPing = millis();
  }


  
  static unsigned long lastWSCheck = 0;
  if (millis() - lastWSCheck > 15000) {
    checkWebSocketHealth();
    lastWSCheck = millis();
  }
  
  // Xử lý phản hồi từ Master (nếu có)
  if (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    if (response.startsWith("RESP:")) {
      String json = response.substring(5);
      String payload = "42[\"command_response\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[RESP] Forwarded to server: " + json);
    }
  }
  
  yield();
  delay(10);
}