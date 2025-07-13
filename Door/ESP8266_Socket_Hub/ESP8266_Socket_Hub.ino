// ========================================
// FIXED ESP8266 Socket Hub - V4.1.1 STABLE
// ========================================

// Trong ESP8266_Socket_Hub.ino, thay thế toàn bộ với code này:

#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;

String HUB_SERIAL = "SERL29JUN2501JYXECBR32V8BD77RW82";
#define FIRMWARE_VERSION "4.1.1"
#define HUB_ID "ESP_HUB_OPT_001"

// ✅ CRITICAL: Managed devices array
String managedDevices[] = {
  "SERL27JUN2501JYR2RKVVX08V40YMGTW",  // Door 1
  "SERL27JUN2501JYR2RKVR0SC7SJ8P8DD",  // Door 2
  "SERL27JUN2501JYR2RKVRNHS46VR6AS1",  // Door 3
  "SERL27JUN2501JYR2RKVSE2RW7KQ4KMP",  // Door 4
  "SERL27JUN2501JYR2RKVTBZ40JPF88WP",  // Door 5
  "SERL27JUN2501JYR2RKVTXNCK1GB3HBZ",  // Door 6
  "SERL27JUN2501JYR2RKVS2P6XBVF1P2E",  // Door 7
  "SERL27JUN2501JYR2RKVTH6PWR9ETXC2",  // Door 8
  "SERL27JUN2501JYR2RKVVSBGRTM0TRFW"   // Door 9
};
const int TOTAL_MANAGED_DEVICES = 9;

WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;
bool devicesRegistered = false; // ✅ CRITICAL: Prevent duplicate registration

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  Serial.println("\n=== ESP Socket Hub STABLE v4.1.1 ===");
  Serial.println("Hub:" + HUB_SERIAL);
  Serial.println("ID:" + String(HUB_ID));
  Serial.println("Managing " + String(TOTAL_MANAGED_DEVICES) + " devices");
  Serial.println("Free Heap:" + String(ESP.getFreeHeap()));
  
  setupWiFi();
  setupWebSocket();
  
  Serial.println("Hub Ready - Enhanced Stability");
  Serial.println("================================\n");
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("WiFi connecting");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✓ CONNECTED");
    Serial.println("IP:" + WiFi.localIP().toString());
    Serial.println("Signal:" + String(WiFi.RSSI()) + "dBm");
  } else {
    Serial.println(" ✗ FAILED");
    ESP.restart();
  }
}

void setupWebSocket() {
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                HUB_SERIAL + "&isIoTDevice=true&hub_managed=true&optimized=true";
  
  Serial.println("[WS] Connecting to " + WEBSOCKET_HOST + ":" + String(WEBSOCKET_PORT));
  
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); // ✅ CRITICAL: Increased interval
  webSocket.enableHeartbeat(25000, 5000, 2); // ✅ CRITICAL: More lenient heartbeat
  
  String userAgent = "ESP-Hub-Opt/4.1.1";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[WS] Setup complete");
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] ✗ DISCONNECTED");
      socketConnected = false;
      devicesRegistered = false; // ✅ CRITICAL: Reset registration flag
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WS] ✓ CONNECTED Hub:" + HUB_SERIAL);
      socketConnected = true;
      lastPingResponse = millis();
      devicesRegistered = false; // ✅ CRITICAL: Reset on new connection
      
      // ✅ CRITICAL: Single registration call
      sendDeviceOnline();
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(String((char*)payload));
      break;
      
    case WStype_ERROR:
      Serial.println("[WS] ✗ ERROR");
      break;
      
    case WStype_PONG:
      lastPingResponse = millis();
      // ✅ CRITICAL: Removed excessive logging
      break;
  }
}

// ✅ CRITICAL: Prevent duplicate registration
void registerManagedDevices() {
  if (devicesRegistered || !socketConnected) return;
  
  Serial.println("[REGISTER] Registering " + String(TOTAL_MANAGED_DEVICES) + " devices...");
  
  for (int i = 0; i < TOTAL_MANAGED_DEVICES; i++) {
    StaticJsonDocument<512> doc;
    doc["deviceId"] = managedDevices[i];
    doc["serialNumber"] = managedDevices[i];
    doc["deviceType"] = "ESP01_SERVO_DOOR";
    doc["hub_controlled"] = true;
    doc["hub_serial"] = HUB_SERIAL;
    doc["connection_type"] = "esp_now_via_hub";
    doc["door_id"] = i + 1;
    doc["firmware_version"] = "3.2.0";
    doc["esp01_device"] = true;

    String payload;
    serializeJson(doc, payload);
    String fullPayload = "42[\"device_online\"," + payload + "]";
    webSocket.sendTXT(fullPayload);
    
    Serial.println("[REGISTER] Device " + String(i + 1) + ": " + managedDevices[i]);
    delay(100); // ✅ CRITICAL: Reduced delay
  }
  
  devicesRegistered = true; // ✅ CRITICAL: Mark as registered
  Serial.println("[REGISTER] ✓ Complete");
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  
  if (type == '0') {
    // Connection established
    if (!devicesRegistered) {
      delay(1000); // ✅ CRITICAL: Wait before registering
      registerManagedDevices();
    }
    
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
    // Reconnection - register devices if not already done
    if (!devicesRegistered) {
      delay(1000);
      registerManagedDevices();
    }
    
  } else if (type == '2') {
    String eventData = data.substring(1);
    handleSocketIOEvent(eventData);
  }
}

void handleSocketIOEvent(String eventData) {
  if (eventData.indexOf("command") != -1) {
    parseAndExecuteCommand(eventData);
    
  } else if (eventData.indexOf("ping") != -1) {
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"hub_serial\":\"" + HUB_SERIAL + "\"}]";
    webSocket.sendTXT(pongPayload);
    lastPingResponse = millis();
  }
}

void parseAndExecuteCommand(String eventData) {
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
  
  // ✅ CRITICAL: Check if device is managed
  bool isManaged = false;
  for (int i = 0; i < TOTAL_MANAGED_DEVICES; i++) {
    if (managedDevices[i] == targetSerial) {
      isManaged = true;
      break;
    }
  }
  
  if (isManaged) {
    Serial.println("[CMD] ✓ " + targetSerial + " -> " + action);
    Serial.println("CMD:" + targetSerial + ":" + action);
  }
}

void sendDeviceOnline() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = HUB_SERIAL;
  doc["serialNumber"] = HUB_SERIAL;
  doc["deviceType"] = "HUB_GATEWAY_OPT";
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["hub_managed"] = true;
  doc["hub_id"] = HUB_ID;
  doc["connection_type"] = "hub_optimized";
  doc["managed_devices_count"] = TOTAL_MANAGED_DEVICES;

  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"device_online\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  Serial.println("[ONLINE] Hub registered");
}

void checkConnectionStatus() {
  static unsigned long lastReconnectAttempt = 0;
  static int reconnectAttempts = 0;
  const int MAX_RECONNECT_ATTEMPTS = 5;
  const unsigned long RECONNECT_INTERVAL = 30000;
  
  if (!socketConnected) {
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
  webSocket.loop();
  checkConnectionStatus();
  
  // ✅ CRITICAL: Reduced ping frequency
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 30000) {
    webSocket.sendTXT("2");
    lastManualPing = millis();
  }

  // Handle MEGA responses
  if (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    response.trim();
    
    if (response.length() == 0) return;
    
    // ✅ CRITICAL: Filter logs to prevent flooding
    if (response.startsWith("HUB_") || 
        response.startsWith("ATMEGA_") ||
        response.indexOf("STATUS") != -1) {
      return; // Don't log these
    }
    
    Serial.println("[MEGA-RX] " + response);
    
    if (response.startsWith("RESP:")) {
      String json = response.substring(5);
      String payload = "42[\"command_response\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("STS:")) {
      String json = response.substring(4);
      String payload = "42[\"deviceStatus\"," + json + "]";
      webSocket.sendTXT(payload);
    }
  }
  
  // ✅ CRITICAL: Reduced status frequency
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    Serial.println("[STATUS] Connected:" + String(socketConnected) + 
                   " | Heap:" + String(ESP.getFreeHeap()) + 
                   " | Uptime:" + String(millis() / 1000) + "s");
    lastStatus = millis();
  }
  
  yield();
  delay(10);
}