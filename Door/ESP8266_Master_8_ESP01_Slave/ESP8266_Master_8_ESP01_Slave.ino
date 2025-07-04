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
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", {0x84, 0x0D, 0x8E, 0xA4, 0x3b, 0xe0}, 1, false, 0, 0},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", {0x3c, 0x71, 0xbf, 0x39, 0x31, 0x2a}, 2, false, 0, 0},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0x9c}, 3, false, 0, 0},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0xa4}, 4, false, 0, 0},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", {0x84, 0x0d, 0x8e, 0xa4, 0x3a, 0xd2}, 5, false, 0, 0}, 
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", {0x84, 0x0d, 0x8e, 0xa4, 0x3b, 0x29}, 6, false, 0, 0},  
};
const int TOTAL_DOORS = 7;

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

void safeStringCopy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP Gateway Master v3.0.4 ===");
  Serial.println("Managing " + String(TOTAL_DOORS) + " ESP-01 Door Controllers");
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
  
  Serial.println("[ESP-NOW] Adding " + String(TOTAL_DOORS) + " ESP-01 peers...");
  
  for (int i = 0; i < TOTAL_DOORS; i++) {
    Serial.print("[ESP-NOW] Door " + String(i + 1) + " MAC: ");
    for (int j = 0; j < 6; j++) {
      Serial.printf("%02X", doors[i].macAddress[j]);
      if (j < 5) Serial.print(":");
    }
    Serial.println();
    
    int result = esp_now_add_peer(doors[i].macAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    if (result != 0) {
      Serial.println("[ESP-NOW] ✗ Door " + String(i + 1) + " peer add failed: " + String(result));
      delay(100);
      result = esp_now_add_peer(doors[i].macAddress, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
      if (result == 0) {
        Serial.println("[ESP-NOW] ✓ Door " + String(i + 1) + " peer added (retry)");
      }
    } else {
      Serial.println("[ESP-NOW] ✓ Door " + String(i + 1) + " peer added");
    }
  }
  
  Serial.println("[ESP-NOW] Setup complete - " + String(TOTAL_DOORS) + " doors configured");
}

int findDoorByMAC(uint8_t *mac_addr) {
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (memcmp(mac_addr, doors[i].macAddress, 6) == 0) {
      return i;
    }
  }
  return -1;
}

int findDoorBySerial(String serialNumber) {
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (doors[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  int doorIndex = findDoorByMAC(mac_addr);
  String doorInfo = (doorIndex >= 0) ? "Door " + String(doorIndex + 1) : "Unknown";
  
  if (sendStatus == 0) {
    Serial.println("[ESP-NOW] ✓ Send OK to " + doorInfo);
  } else {
    Serial.println("[ESP-NOW] ✗ Send FAILED to " + doorInfo + ": " + String(sendStatus));
  }
}

void onDataReceived(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  int doorIndex = findDoorByMAC(mac_addr);
  
  if (doorIndex < 0) {
    Serial.println("[ESP-NOW] Unknown sender");
    return;
  }
  
  if (len != sizeof(ESPNowMessage)) {
    Serial.println("[ESP-NOW] Invalid size from Door " + String(doorIndex + 1) + ": " + String(len));
    return;
  }
  
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  memcpy(&receiveBuffer, incomingData, len);
  
  doors[doorIndex].lastSeen = millis();
  doors[doorIndex].isOnline = true;
  
  String msgType = String(receiveBuffer.messageType);
  String doorSerial = String(receiveBuffer.serialNumber);
  
  Serial.println("[ESP-NOW] RX from Door " + String(doorIndex + 1) + " (" + doorSerial + "): " + msgType);
  
  if (msgType == "heartbeat") {
    Serial.println("[HEARTBEAT] Door " + String(doorIndex + 1) + " alive (angle: " + String(receiveBuffer.servoAngle) + "°)");
    
  } else if (msgType == "cmd_resp") {
    String action = String(receiveBuffer.action);
    bool success = receiveBuffer.success;
    String result = String(receiveBuffer.result);
    
    Serial.println("[RECV] Door " + String(doorIndex + 1) + " response: " + action + " = " + String(success));
    sendCommandResponse(doorSerial, action, success, result);
    
  } else if (msgType == "status") {
    String json = "{";
    json += "\"deviceId\":\"" + doorSerial + "\",";
    json += "\"type\":\"deviceStatus\",";
    json += "\"servo_angle\":" + String(receiveBuffer.servoAngle) + ",";
    json += "\"door_state\":\"" + String(receiveBuffer.result) + "\",";
    json += "\"esp01_online\":true,";
    json += "\"connection_type\":\"gateway\",";
    json += "\"door_index\":" + String(doorIndex + 1) + ",";
    json += "\"timestamp\":" + String(receiveBuffer.timestamp);
    json += "}";
    
    Serial.println("RESP:" + json);
  }
}

void forwardCommandToESP01(String serialNumber, String action) {
  int doorIndex = findDoorBySerial(serialNumber);
  
  if (doorIndex < 0) {
    Serial.println("[WARN] Door not found: " + serialNumber);
    sendCommandResponse(serialNumber, action, false, "Door not found");
    return;
  }
  
  if (!doors[doorIndex].isOnline) {
    Serial.println("[WARN] Door " + String(doorIndex + 1) + " offline, cannot send command");
    sendCommandResponse(serialNumber, action, false, "ESP-01 offline");
    return;
  }
  
  // Kiểm tra độ dài dữ liệu để tránh cắt ngắn
  if (serialNumber.length() >= 32) {
    Serial.println("[WARN] Serial number too long: " + serialNumber);
    sendCommandResponse(serialNumber, action, false, "Serial number too long");
    return;
  }
  if (action.length() >= 16) {
    Serial.println("[WARN] Action too long: " + action);
    sendCommandResponse(serialNumber, action, false, "Action too long");
    return;
  }
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "command", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.serialNumber, serialNumber.c_str(), sizeof(sendBuffer.serialNumber));
  safeStringCopy(sendBuffer.action, action.c_str(), sizeof(sendBuffer.action));
  
  sendBuffer.servoAngle = (action == "open_door") ? 180 : 0;
  sendBuffer.success = false;
  safeStringCopy(sendBuffer.result, "", sizeof(sendBuffer.result));
  sendBuffer.timestamp = millis();
  
  int result = esp_now_send(doors[doorIndex].macAddress, (uint8_t*)&sendBuffer, sizeof(ESPNowMessage));
  
  if (result == 0) {
    Serial.println("[ESP-NOW] ✓ Command sent to Door " + String(doorIndex + 1) + ": " + action);
  } else {
    Serial.println("[ESP-NOW] ✗ Command failed to Door " + String(doorIndex + 1) + ": " + String(result));
    sendCommandResponse(serialNumber, action, false, "ESP-NOW failed");
  }
}

void sendCommandResponse(String serialNumber, String command, bool success, String result) {
  // Kiểm tra độ dài chuỗi
  char serialBuf[32];
  char commandBuf[16];
  char resultBuf[32];
  safeStringCopy(serialBuf, serialNumber.c_str(), sizeof(serialBuf));
  safeStringCopy(commandBuf, command.c_str(), sizeof(commandBuf));
  safeStringCopy(resultBuf, result.c_str(), sizeof(resultBuf));
  
  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"result\":\"" + String(resultBuf) + "\",";
  json += "\"deviceId\":\"" + String(serialBuf) + "\",";
  json += "\"command\":\"" + String(commandBuf) + "\",";
  json += "\"gateway_processed\":true,";
  json += "\"servo_angle\":0,";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  Serial.println("RESP:" + json);
}

void sendHeartbeatToESP01(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= TOTAL_DOORS) return;
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "heartbeat", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.serialNumber, doors[doorIndex].serialNumber.c_str(), sizeof(sendBuffer.serialNumber));
  safeStringCopy(sendBuffer.result, "gateway_ping", sizeof(sendBuffer.result));
  
  sendBuffer.servoAngle = 0;
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  int result = esp_now_send(doors[doorIndex].macAddress, (uint8_t*)&sendBuffer, sizeof(ESPNowMessage));
  if (result == 0) {
    Serial.println("[HEARTBEAT] ✓ Sent to Door " + String(doorIndex + 1));
    doors[doorIndex].lastHeartbeatSent = millis();
  } else {
    Serial.println("[HEARTBEAT] ✗ Failed to Door " + String(doorIndex + 1) + ": " + String(result));
  }
}

void checkESP01Status() {
  unsigned long now = millis();
  
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (doors[i].isOnline && (now - doors[i].lastSeen > 180000)) {
      doors[i].isOnline = false;
      Serial.println("[TIMEOUT] Door " + String(i + 1) + " (" + doors[i].serialNumber + ") offline");
    }
  }
}

void printDoorConfiguration() {
  Serial.println("\n[CONFIG] Door Configuration:");
  for (int i = 0; i < TOTAL_DOORS; i++) {
    Serial.println("Door" + String(i + 1) + ": " + doors[i].serialNumber);
    Serial.print("MAC: ");
    for (int j = 0; j < 6; j++) {
      Serial.printf("%02X", doors[i].macAddress[j]);
      if (j < 5) Serial.print(":");
    }
    Serial.println(" | Online: " + String(doors[i].isOnline ? "Yes" : "No"));
  }
  Serial.println();
}

void printStatus() {
  Serial.println("\n[STATUS] Gateway Status:");
  int onlineCount = 0;
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (doors[i].isOnline) onlineCount++;
  }
  Serial.println("ESP-01 Online: " + String(onlineCount) + "/" + String(TOTAL_DOORS));
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("Uptime: " + String(millis() / 1000) + "s");
  Serial.println();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.startsWith("CMD:")) {
      String cmdData = cmd.substring(4);
      int colonIndex = cmdData.indexOf(':');
      if (colonIndex > 0) {
        String serialNumber = cmdData.substring(0, colonIndex);
        String action = cmdData.substring(colonIndex + 1);
        Serial.println("[COMMAND] Received: " + serialNumber + " -> " + action);
        forwardCommandToESP01(serialNumber, action);
      } else {
        String action = cmdData;
        Serial.println("[COMMAND] Legacy format, using Door 1: " + action);
        forwardCommandToESP01(doors[0].serialNumber, action);
      }
    }
  }
  
  static unsigned long lastHeartbeat = 0;
  static int heartbeatDoorIndex = 0;
  
  if (millis() - lastHeartbeat > 10000) {
    if (doors[heartbeatDoorIndex].isOnline || 
        (millis() - doors[heartbeatDoorIndex].lastHeartbeatSent > 120000)) {
      sendHeartbeatToESP01(heartbeatDoorIndex);
    }
    
    heartbeatDoorIndex = (heartbeatDoorIndex + 1) % TOTAL_DOORS;
    lastHeartbeat = millis();
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