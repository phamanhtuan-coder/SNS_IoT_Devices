#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;

// HUB IDENTITY
String HUB_SERIAL = "SERL29JUN2501JYXECBR32V8BD77RW82";
#define FIRMWARE_VERSION "4.0.0"
#define HUB_ID "ESP_HUB_OPT_001"

WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;

// Compact command tracking
struct CompactCommand {
  String action;
  String targetSerial;
  unsigned long timestamp;
  bool processed;
};

CompactCommand lastCommand;

// Prototype declaration
String expandCompactResponse(String compactJson);

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

void checkConnectionStatus() {
  if (!socketConnected) {
    Serial.println("[STATUS] Socket not connected, attempting reconnection...");
    
    // Force reconnect if disconnected for too long
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 10000) {
      Serial.println("[RECONNECT] Manual reconnection attempt");
      webSocket.disconnect();
      delay(1000);
      setupWebSocket();
      lastReconnectAttempt = millis();
    }
  }
}

void setupWebSocket() {
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                HUB_SERIAL + "&isIoTDevice=true&hub_managed=true&optimized=true";
  
  // Add connection retry logic
  Serial.println("[WS] Setting up WebSocket connection...");
  
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(2000);
  webSocket.enableHeartbeat(20000, 3000, 2);
  
  String userAgent = "ESP-Hub-Opt/4.0.0";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[SETUP] WebSocket path: " + path);
  Serial.println("[SETUP] User-Agent: " + userAgent);
  
  // Wait for initial connection attempt
  delay(1000);
}


void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] DISCONNECTED");
      socketConnected = false;
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
      Serial.println("[WS] PONG received");
      break;
  }
}

void handleWebSocketMessage(String message) {
  // Remove debug prints that go to Serial (Mega)
  
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  
  if (type == '0') {
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
    sendDeviceOnline();
    
  } else if (type == '2') {
    String eventData = data.substring(1);
    handleSocketIOEvent(eventData);
  }
}


void handleSocketIOEvent(String eventData) {
  if (eventData.indexOf("command") != -1) {
    parseAndExecuteOptimizedCommand(eventData);
    
  } else if (eventData.indexOf("ping") != -1) {
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"hub_serial\":\"" + HUB_SERIAL + "\",\"optimized\":true}]";
    webSocket.sendTXT(pongPayload);
    lastPingResponse = millis();
    
  } else if (eventData.indexOf("door_command") != -1) {
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


// Replace your parseAndExecuteOptimizedCommand function with this:

void parseAndExecuteOptimizedCommand(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx == -1 || endIdx == -1) return;
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  JsonDocument doc;
  if (deserializeJson(doc, jsonString) != DeserializationError::Ok) return;
  
  String targetSerial = "";
  if (doc["serialNumber"].is<String>()) {
    targetSerial = doc["serialNumber"].as<String>();
  }
  
  String action = "";
  if (doc["action"].is<String>()) {
    action = doc["action"].as<String>();
  }
  
  if (targetSerial == "" || action == "") return;
  
  // ✅ Send ONLY clean command to Mega
  Serial.println("CMD:" + targetSerial + ":" + action);
}

void sendDeviceOnline() {
  if (!socketConnected) {
    Serial.println("[ONLINE] Not connected, skipping");
    return;
  }
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = HUB_SERIAL;
  doc["serialNumber"] = HUB_SERIAL;
  doc["deviceType"] = "HUB_GATEWAY_OPT";
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["hub_managed"] = true;
  doc["hub_id"] = HUB_ID;
  doc["connection_type"] = "hub_optimized";
  doc["esp01_optimized"] = true;
  doc["compact_data"] = true;

  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"device_online\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  Serial.println("[ONLINE] Sent device_online: " + fullPayload);
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

   // Check connection status first
  checkConnectionStatus();
  
  // Send ping every 20s
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 20000) {
    webSocket.sendTXT("2");
    lastManualPing = millis();
    Serial.println("[LOOP] Sent manual PING");
  }


  static unsigned long lastWSCheck = 0;
  if (millis() - lastWSCheck > 10000) {
    checkWebSocketHealth();
    lastWSCheck = millis();
  }
  
  // Handle compact responses from MEGA
  if (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    response.trim();
    
    if (response.length() == 0) return;
    
    Serial.println("[MEGA-RX] " + response);
    
    if (response.startsWith("RESP:")) {
      String json = response.substring(5);
      
      Serial.println("[RESP] Processing response");
      
      if (json.indexOf("\"s\":") != -1 && json.indexOf("\"d\":") != -1) {
        String expandedJson = expandCompactResponse(json);
        String payload = "42[\"command_response\"," + expandedJson + "]";
        webSocket.sendTXT(payload);
        
        Serial.println("[RESP] Compact expanded and sent: " + payload);
      } else {
        String payload = "42[\"command_response\"," + json + "]";
        webSocket.sendTXT(payload);
        
        Serial.println("[RESP] Standard sent: " + payload);
      }
    } else if (response.startsWith("STS:")) {
      String json = response.substring(4);
      
      String payload = "42[\"deviceStatus\"," + json + "]";
      webSocket.sendTXT(payload);
      
      Serial.println("[STATUS] Forwarded: " + payload);
    }
  }
  
  // Status print
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    Serial.println("\n[STATUS] Hub: " + HUB_SERIAL);
    Serial.println("[STATUS] Connected: " + String(socketConnected));
    Serial.println("[STATUS] Uptime: " + String(millis() / 1000) + "s\n");
    lastStatus = millis();
  }
  
  yield();
  delay(5);
}

String expandCompactResponse(String compactJson) {
  JsonDocument doc;
  if (deserializeJson(doc, compactJson) != DeserializationError::Ok) {
    Serial.println("[EXPAND] Parse error, returning original");
    return compactJson;
  }
  
  String success = doc["s"].is<String>() ? doc["s"].as<String>() : (doc["s"].as<int>() == 1 ? "true" : "false");
  String result = doc["r"].is<String>() ? doc["r"].as<String>() : "processed";
  String deviceId = doc["d"].is<String>() ? doc["d"].as<String>() : "";
  String command = doc["c"].is<String>() ? doc["c"].as<String>() : "";
  int angle = doc["a"].is<int>() ? doc["a"].as<int>() : 0;
  unsigned long timestamp = doc["t"].is<unsigned long>() ? doc["t"].as<unsigned long>() : millis();
  
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