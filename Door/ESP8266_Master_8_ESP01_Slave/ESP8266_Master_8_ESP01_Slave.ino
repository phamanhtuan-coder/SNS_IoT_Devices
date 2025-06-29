#define FIRMWARE_VERSION "3.0.4"
#define GATEWAY_ID "ESP_GATEWAY_001"

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ArduinoJson.h>

struct DoorConfig {
  String serialNumber;
  uint8_t macAddress[6];
  int connectionId;
  bool isOnline;
  unsigned long lastSeen;
  unsigned long lastHeartbeatSent;
};

DoorConfig doors[7] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", {0x84, 0x0D, 0x8E, 0xA4, 0x91, 0x58}, 0, false, 0, 0},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", {0x84, 0x0D, 0x8E, 0xA4, 0x3b, 0xe0}, 0, false, 0, 0},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", {0x3c, 0x71, 0xbf, 0x39, 0x31, 0x2a}, 0, false, 0, 0},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0x9c}, 0, false, 0, 0},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0xa4}, 0, false, 0, 0},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0xa4}, 0, false, 0, 0}, // chip 6 hư, thay sau
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E",  {0x84, 0x0d, 0x8e, 0xa4, 0x3b, 0x29}, 0, false, 0, 0},  
};
const int TOTAL_DOORS = 1;

struct ESPNowMessage {
  char messageType[16];
  char serialNumber[32];
  char action[16];
  int servoAngle;
  bool success;
  char result[32];
  unsigned long timestamp;
};

static ESPNowMessage sendBuffer;
static ESPNowMessage receiveBuffer;

// ✅ SAFE STRING COPY HELPER
void safeStringCopy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';  // Ensure null termination
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP Gateway Master v3.0.4 ===");
  Serial.println("Managing 1 ESP-01 Door Controller");
  Serial.println("Gateway ID: " + String(GATEWAY_ID));
  Serial.println("Gateway MAC: " + WiFi.macAddress());
  
  setupESPNow();
  
  Serial.println("[INIT] Gateway Ready");
  printDoorConfiguration();
}

void setupESPNow() {
  Serial.println("[ESP-NOW] Initializing...");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(1);
  wifi_set_sleep_type(NONE_SLEEP_T);
  Serial.println("[ESP-NOW] Channel: 1, Sleep: NONE");
  
  int initResult = esp_now_init();
  if (initResult != 0) {
    Serial.println("[ESP-NOW] Init FAILED: " + String(initResult));
    delay(5000);
    ESP.restart();
  }
  
  Serial.println("[ESP-NOW] Init OK");
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  Serial.println("[ESP-NOW] Adding ESP-01 peer...");
  Serial.print("[ESP-NOW] MAC: ");
  for (int j = 0; j < 6; j++) {
    Serial.printf("%02X", doors[0].macAddress[j]);
    if (j < 5) Serial.print(":");
  }
  Serial.println();
  
  int result = esp_now_add_peer(doors[0].macAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  if (result != 0) {
    Serial.println("[ESP-NOW] ✗ Peer add failed: " + String(result));
    delay(100);
    result = esp_now_add_peer(doors[0].macAddress, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
    if (result == 0) {
      Serial.println("[ESP-NOW] ✓ ESP-01 peer added (retry)");
    }
  } else {
    Serial.println("[ESP-NOW] ✓ ESP-01 peer added");
  }
  
  Serial.println("[ESP-NOW] Setup complete");
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    Serial.println("[ESP-NOW] ✓ Send OK");
  } else {
    Serial.println("[ESP-NOW] ✗ Send FAILED: " + String(sendStatus));
  }
}

void onDataReceived(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  if (memcmp(mac_addr, doors[0].macAddress, 6) != 0) {
    Serial.println("[ESP-NOW] Unknown sender");
    return;
  }
  
  if (len != sizeof(ESPNowMessage)) {
    Serial.println("[ESP-NOW] Invalid size: " + String(len));
    return;
  }
  
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  memcpy(&receiveBuffer, incomingData, len);
  
  doors[0].lastSeen = millis();
  doors[0].isOnline = true;
  
  String msgType = String(receiveBuffer.messageType);
  Serial.println("[ESP-NOW] RX: " + msgType);
  
  if (msgType == "heartbeat") {
    Serial.println("[HEARTBEAT] ESP-01 alive (angle: " + String(receiveBuffer.servoAngle) + "°)");
    
  } else if (msgType == "cmd_resp") {
    String action = String(receiveBuffer.action);
    bool success = receiveBuffer.success;
    String result = String(receiveBuffer.result);
    
    Serial.println("[RECV] Command response: " + action + " = " + String(success));
    sendCommandResponse(action, success, result);
    
  } else if (msgType == "status") {
    String json = "{";
    json += "\"deviceId\":\"" + doors[0].serialNumber + "\",";
    json += "\"type\":\"deviceStatus\",";
    json += "\"servo_angle\":" + String(receiveBuffer.servoAngle) + ",";
    json += "\"door_state\":\"" + String(receiveBuffer.result) + "\",";
    json += "\"esp01_online\":true,";
    json += "\"connection_type\":\"gateway\",";
    json += "\"timestamp\":" + String(receiveBuffer.timestamp);
    json += "}";
    
    Serial.println("RESP:" + json);
  }
}

void forwardCommandToESP01(String action) {
  if (!doors[0].isOnline) {
    Serial.println("[WARN] ESP-01 offline, cannot send command");
    sendCommandResponse(action, false, "ESP-01 offline");
    return;
  }
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  // ✅ FIXED: Use safe string copying
  safeStringCopy(sendBuffer.messageType, "command", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.serialNumber, doors[0].serialNumber.c_str(), sizeof(sendBuffer.serialNumber));
  safeStringCopy(sendBuffer.action, action.c_str(), sizeof(sendBuffer.action));
  
  sendBuffer.servoAngle = 0;
  sendBuffer.success = false;
  sendBuffer.timestamp = millis();
  
  delay(10);
  int result = esp_now_send(doors[0].macAddress, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[ESP-NOW] ✓ Command sent: " + action);
  } else {
    Serial.println("[ESP-NOW] ✗ Command failed: " + String(result));
    sendCommandResponse(action, false, "ESP-NOW failed");
  }
}

void sendCommandResponse(String command, bool success, String result) {
  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"result\":\"" + result + "\",";
  json += "\"deviceId\":\"" + doors[0].serialNumber + "\",";
  json += "\"command\":\"" + command + "\",";
  json += "\"gateway_processed\":true,";
  json += "\"servo_angle\":0,";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  Serial.println("RESP:" + json);
}

void sendHeartbeatToESP01() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  // ✅ FIXED: Use safe string copying
  safeStringCopy(sendBuffer.messageType, "heartbeat", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.serialNumber, doors[0].serialNumber.c_str(), sizeof(sendBuffer.serialNumber));
  safeStringCopy(sendBuffer.result, "gateway_ping", sizeof(sendBuffer.result));
  
  sendBuffer.servoAngle = 0;
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  delay(20);
  int result = esp_now_send(doors[0].macAddress, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  if (result == 0) {
    Serial.println("[HEARTBEAT] ✓ Sent to ESP-01");
    doors[0].lastHeartbeatSent = millis();
  } else {
    Serial.println("[HEARTBEAT] ✗ Failed: " + String(result));
  }
}

void checkESP01Status() {
  unsigned long now = millis();
  
  if (doors[0].isOnline && (now - doors[0].lastSeen > 180000)) {
    doors[0].isOnline = false;
    Serial.println("[TIMEOUT] ESP-01 offline");
  }
}

void printDoorConfiguration() {
  Serial.println("\n[CONFIG] Door Configuration:");
  Serial.println("Door1: " + doors[0].serialNumber);
  Serial.print("MAC: ");
  for (int j = 0; j < 6; j++) {
    Serial.printf("%02X", doors[0].macAddress[j]);
    if (j < 5) Serial.print(":");
  }
  Serial.println("\nOnline: " + String(doors[0].isOnline ? "Yes" : "No"));
  Serial.println();
}

void printStatus() {
  Serial.println("\n[STATUS] Gateway Status:");
  Serial.println("ESP-01 Online: " + String(doors[0].isOnline ? "Yes" : "No"));
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("Uptime: " + String(millis() / 1000) + "s");
  Serial.println();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    if (cmd.startsWith("CMD:")) {
      String action = cmd.substring(4);
      action.trim(); // ✅ ADDED: Remove whitespace
      forwardCommandToESP01(action);
    }
  }
  
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {
    if (doors[0].isOnline || (millis() - doors[0].lastHeartbeatSent > 120000)) {
      sendHeartbeatToESP01();
      lastHeartbeat = millis();
    }
  }
  
  static unsigned long lastESP01Check = 0;
  if (millis() - lastESP01Check > 30000) {
    checkESP01Status();
    lastESP01Check = millis();
  }
  
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate > 120000) {
    printStatus();
    lastStatusUpdate = millis();
  }
  
  yield();
  delay(10);
}