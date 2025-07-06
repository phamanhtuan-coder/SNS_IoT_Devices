#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;

// ✅ HUB IDENTITY - Optimized Hub
String HUB_SERIAL = "SERL29JUN2501JYXECBR32V8BD77RW82"; // Hub Socket Mô Hình 
#define FIRMWARE_VERSION "4.0.0"
#define HUB_ID "ESP_HUB_OPT_001"

WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;
bool welcomeReceived = false;

// ✅ OPTIMIZED: Compact command tracking
struct CompactCommand {
  String action;
  String targetSerial;
  unsigned long timestamp;
  bool processed;
};

CompactCommand lastCommand;

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  Serial.println("\n=== ESP Socket Hub OPT v4.0.0 ===");
  Serial.println("Hub:" + HUB_SERIAL);
  Serial.println("ID:" + String(HUB_ID));
  Serial.println("Mode: ESP-01 Optimized");
  
  setupWiFi();
  setupWebSocket();
  
  Serial.println("Hub Ready - Optimized");
  Serial.println("================================\n");
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("WiFi...");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK");
    Serial.println("IP:" + WiFi.localIP().toString());
  } else {
    Serial.println(" FAIL");
    ESP.restart();
  }
}

void setupWebSocket() {
  // ✅ OPTIMIZED: Connect as optimized hub
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                HUB_SERIAL + "&isIoTDevice=true&hub_managed=true&optimized=true";
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(2000);
  webSocket.enableHeartbeat(20000, 3000, 2);
  
  String userAgent = "ESP-Hub-Opt/4.0.0";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[SETUP] WebSocket path: " + path);
  Serial.println("[SETUP] User-Agent: " + userAgent);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] DISCONNECTED");
      socketConnected = false;
      welcomeReceived = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WS] CONNECTED Hub:" + HUB_SERIAL);
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
      lastPingResponse = millis();
      break;
  }
}

void handleWebSocketMessage(String message) {
  Serial.println("[WS-RX] " + message);
  
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  
  if (type == '0') {
    Serial.println("[EIO] OPEN");
    sendDeviceOnline();
    
  } else if (type == '2') {
    webSocket.sendTXT("3");
    lastPingResponse = millis();
    
  } else if (type == '3') {
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
  Serial.println("[EVENT] " + eventData);
  
  if (eventData.indexOf("connection_welcome") != -1) {
    Serial.println("[HUB] Welcome received!");
    welcomeReceived = true;
    
    // Send acknowledgment with serial number
    String ackPayload = "42[\"welcome_ack\",{\"received\":true,\"hub_managed\":true,\"optimized\":true,\"serialNumber\":\"" + HUB_SERIAL + "\"}]";
    webSocket.sendTXT(ackPayload);
    Serial.println("[HUB] Welcome ACK sent");
    
  } else if (eventData.indexOf("command") != -1) {
    Serial.println("[CMD] Command event received");
    parseAndExecuteOptimizedCommand(eventData);
    
  } else if (eventData.indexOf("ping") != -1) {
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"hub_serial\":\"" + HUB_SERIAL + "\",\"optimized\":true}]";
    webSocket.sendTXT(pongPayload);
    Serial.println("[PING] Pong sent");
    lastPingResponse = millis();
    
  } else if (eventData.indexOf("door_command") != -1) {
    Serial.println("[DOOR] Door command received");
    extractOptimizedAction(eventData);
  }
}

void extractOptimizedAction(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx != -1 && endIdx != -1) {
    String jsonString = eventData.substring(startIdx, endIdx + 1);
    Serial.println("[EXTRACT] JSON: " + jsonString);
    
    JsonDocument doc;
    if (deserializeJson(doc, jsonString) == DeserializationError::Ok) {
      if (doc["action"].is<String>()) {
        lastCommand.action = doc["action"].as<String>();
        lastCommand.timestamp = millis();
        lastCommand.processed = false;
        Serial.println("[EXTRACT] Action: " + lastCommand.action);
      }
      
      if (doc["serialNumber"].is<String>()) {
        lastCommand.targetSerial = doc["serialNumber"].as<String>();
        Serial.println("[EXTRACT] Target: " + lastCommand.targetSerial);
      }
    }
  }
}

// ✅ OPTIMIZED: Parse and execute commands with compact format
void parseAndExecuteOptimizedCommand(String eventData) {
  Serial.println("[PARSE] Starting parse...");
  
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx == -1 || endIdx == -1) {
    Serial.println("[PARSE] No JSON found");
    return;
  }
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  Serial.println("[PARSE] JSON: " + jsonString);
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.println("[PARSE] JSON Error: " + String(error.c_str()));
    return;
  }
  
  // ✅ OPTIMIZED: Extract target device
  String targetSerial = "";
  if (doc["serialNumber"].is<String>()) {
    targetSerial = doc["serialNumber"].as<String>();
  }
  
  // ✅ OPTIMIZED: Extract action with fallback
  String action = "";
  
  if (doc["esp01_safe"].is<bool>() && doc["esp01_safe"] == true) {
    Serial.println("[PARSE] ESP01 Safe mode");
    
    action = "toggle_door"; // Default
    
    // Use stored action if recent
    if (millis() - lastCommand.timestamp < 3000 && lastCommand.action != "") {
      action = lastCommand.action;
      Serial.println("[PARSE] Using stored action: " + action);
    }
    
    if (doc["action"].is<String>()) {
      action = doc["action"].as<String>();
    }
    
  } else {
    if (doc["action"].is<String>()) {
      action = doc["action"].as<String>();
    }
  }
  
  // Use stored target if not in current command
  if (targetSerial == "" && lastCommand.targetSerial != "" && millis() - lastCommand.timestamp < 3000) {
    targetSerial = lastCommand.targetSerial;
    Serial.println("[PARSE] Using stored target: " + targetSerial);
  }
  
  if (targetSerial == "") {
    Serial.println("[PARSE] No target serial");
    return;
  }
  
  if (action == "") {
    action = "toggle_door";
    Serial.println("[PARSE] Using default action");
  }
  
  Serial.println("[PARSE] Final: " + targetSerial + " -> " + action);
  
  // ✅ OPTIMIZED: Send compact command to MEGA
  String megaCommand = "CMD:" + targetSerial + ":" + action;
  Serial.println(megaCommand);
  Serial.println("[MEGA-TX] " + megaCommand);
}

void sendDeviceOnline() {
  if (!socketConnected) {
    Serial.println("[ONLINE] Not connected, skipping");
    return;
  }
  
  // ✅ OPTIMIZED: Compact device online format
  String json = "{";
  json += "\"deviceId\":\"" + HUB_SERIAL + "\",";
  json += "\"serialNumber\":\"" + HUB_SERIAL + "\",";
  json += "\"deviceType\":\"HUB_GATEWAY_OPT\",";
  json += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"hub_managed\":true,";
  json += "\"hub_id\":\"" + String(HUB_ID) + "\",";
  json += "\"connection_type\":\"hub_optimized\",";
  json += "\"esp01_optimized\":true,";
  json += "\"compact_data\":true";
  json += "}";
  
  String payload = "42[\"device_online\"," + json + "]";
  webSocket.sendTXT(payload);
  
  Serial.println("[ONLINE] Sent device_online");
}

void checkWebSocketHealth() {
  unsigned long now = millis();
  
  if (socketConnected && (now - lastPingResponse > 30000)) {
    Serial.println("[HEALTH] WS Timeout, reconnecting...");
    webSocket.disconnect();
  }
}

void loop() {
  webSocket.loop(); 
  
  // Send ping every 15s
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 15000) {
    webSocket.sendTXT("2");
    lastManualPing = millis();
  }

  static unsigned long lastWSCheck = 0;
  if (millis() - lastWSCheck > 10000) {
    checkWebSocketHealth();
    lastWSCheck = millis();
  }
  
  // ✅ OPTIMIZED: Handle compact responses from MEGA
  if (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    response.trim();
    
    if (response.length() == 0) return;
    
    Serial.println("[MEGA-RX] " + response);
    
    if (response.startsWith("RESP:")) {
      String json = response.substring(5);
      
      Serial.println("[RESP] Processing response");
      
      // ✅ OPTIMIZED: Check if response is compact and expand if needed
      if (json.indexOf("\"s\":") != -1 && json.indexOf("\"d\":") != -1) {
        // This is compact format, expand it
        String expandedJson = expandCompactResponse(json);
        String payload = "42[\"command_response\"," + expandedJson + "]";
        webSocket.sendTXT(payload);
        
        Serial.println("[RESP] Compact expanded and sent");
      } else {
        // Standard format
        String payload = "42[\"command_response\"," + json + "]";
        webSocket.sendTXT(payload);
        
        Serial.println("[RESP] Standard sent");
      }
    } else if (response.startsWith("STS:")) {
      String json = response.substring(4);
      
      // Forward status update
      String payload = "42[\"deviceStatus\"," + json + "]";
      webSocket.sendTXT(payload);
      
      Serial.println("[STATUS] Forwarded");
    }
  }
  
  // Status print
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    Serial.println("\n[STATUS] Hub: " + HUB_SERIAL);
    Serial.println("[STATUS] Connected: " + String(socketConnected));
    Serial.println("[STATUS] Welcome: " + String(welcomeReceived));
    Serial.println("[STATUS] Uptime: " + String(millis() / 1000) + "s\n");
    lastStatus = millis();
  }
  
  yield();
  delay(5);
}

// ✅ OPTIMIZED: Expand compact response to full format
String expandCompactResponse(String compactJson) {
  JsonDocument doc;
  if (deserializeJson(doc, compactJson) != DeserializationError::Ok) {
    Serial.println("[EXPAND] Parse error, returning original");
    return compactJson; // Return as-is if parse fails
  }
  
  // Extract compact fields
  String success = doc["s"].is<String>() ? doc["s"].as<String>() : (doc["s"].as<int>() == 1 ? "true" : "false");
  String result = doc["r"].is<String>() ? doc["r"].as<String>() : "processed";
  String deviceId = doc["d"].is<String>() ? doc["d"].as<String>() : "";
  String command = doc["c"].is<String>() ? doc["c"].as<String>() : "";
  int angle = doc["a"].is<int>() ? doc["a"].as<int>() : 0;
  unsigned long timestamp = doc["t"].is<unsigned long>() ? doc["t"].as<unsigned long>() : millis();
  
  // Create expanded JSON
  String expanded = "{";
  expanded += "\"success\":" + success + ",";
  expanded += "\"result\":\"" + result + "\",";
  expanded += "\"deviceId\":\"" + deviceId + "\",";
  expanded += "\"command\":\"" + command + "\",";
  expanded += "\"servo_angle\":" + String(angle) + ",";
  expanded += "\"esp01_processed\":true,";
  expanded += "\"optimized\":true,";
  expanded += "\"timestamp\":" + String(timestamp);
  expanded += "}";
  
  Serial.println("[EXPAND] Result: " + expanded);
  return expanded;
}