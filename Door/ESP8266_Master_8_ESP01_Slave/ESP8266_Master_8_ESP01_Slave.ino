#define FIRMWARE_VERSION "4.0.0"
#define GATEWAY_ID "ESP_GATEWAY_002"

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ArduinoJson.h>

// ✅ OPTIMIZED: Compact message structure matching ESP-01
struct CompactMessage {
  char type[4];        // "CMD", "ACK", "HBT", "STS"
  char action[4];      // "OPN", "CLS", "TGL", "ALV", etc.
  int angle;           // Servo angle
  bool success;        // Success flag
  unsigned long ts;    // Timestamp
};

// ✅ OPTIMIZED: Simplified door configuration
struct DoorConfig {
  String serialNumber;
  uint8_t macAddress[6];
  int doorId;
  bool isOnline;
  unsigned long lastSeen;
  int currentAngle;
  String doorState;
};

DoorConfig doors[7] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", {0x84, 0x0D, 0x8E, 0xA4, 0x91, 0x58}, 1, false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", {0x84, 0x0D, 0x8E, 0xA4, 0x3b, 0xe0}, 2, false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", {0x3c, 0x71, 0xbf, 0x39, 0x31, 0x2a}, 3, false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0x9c}, 4, false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0xa4}, 5, false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", {0x84, 0x0d, 0x8e, 0xa4, 0x3a, 0xd2}, 6, false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", {0x84, 0x0d, 0x8e, 0xa4, 0x3b, 0x29}, 7, false, 0, 0, "CLD"}
};
const int TOTAL_DOORS = 7;

static CompactMessage sendBuffer;
static CompactMessage receiveBuffer;

// ✅ OPTIMIZED: Action mapping for compact commands
String mapActionToCompact(String action) {
  if (action == "open_door") return "OPN";
  if (action == "close_door") return "CLS";
  if (action == "toggle_door") return "TGL";
  return "UNK";  // Unknown
}

String mapCompactToFull(String compact) {
  if (compact == "OPN") return "open_door";
  if (compact == "CLS") return "close_door"; 
  if (compact == "TGL") return "toggle_door";
  if (compact == "ALV") return "alive";
  return compact;
}

String mapStateToFull(String compact) {
  if (compact == "CLD") return "closed";
  if (compact == "OPG") return "opening";
  if (compact == "OPD") return "open";
  if (compact == "CLG") return "closing";
  return "unknown";
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  Serial.println("ESP Gateway v4.0.0");
  Serial.println("ID:" + String(GATEWAY_ID));
  Serial.println("Doors:" + String(TOTAL_DOORS));
  Serial.println("MAC:" + WiFi.macAddress());
  
  setupESPNow();
  
  Serial.println("Gateway Ready");
}

void setupESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(1);
  wifi_set_sleep_type(NONE_SLEEP_T);
  
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init FAIL");
    delay(3000);
    ESP.restart();
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  // Add all ESP-01 peers
  for (int i = 0; i < TOTAL_DOORS; i++) {
    int result = esp_now_add_peer(doors[i].macAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    if (result == 0) {
      Serial.println("Door" + String(i + 1) + " Peer OK");
    } else {
      Serial.println("Door" + String(i + 1) + " Peer FAIL:" + String(result));
    }
  }
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
  if (sendStatus == 0) {
    Serial.println("TX OK Door" + String(doorIndex + 1));
  } else {
    Serial.println("TX FAIL Door" + String(doorIndex + 1) + ":" + String(sendStatus));
  }
}

void onDataReceived(uint8_t *mac_addr, uint8_t *data, uint8_t len) {
  int doorIndex = findDoorByMAC(mac_addr);
  
  if (doorIndex < 0 || len != sizeof(CompactMessage)) {
    Serial.println("RX Invalid");
    return;
  }
  
  memcpy(&receiveBuffer, data, len);
  
  doors[doorIndex].lastSeen = millis();
  doors[doorIndex].isOnline = true;
  doors[doorIndex].currentAngle = receiveBuffer.angle;
  
  String msgType = String(receiveBuffer.type);
  String action = String(receiveBuffer.action);
  
  Serial.println("RX Door" + String(doorIndex + 1) + ":" + msgType + ":" + action);
  
  if (msgType == "HBT") {
    Serial.println("HBT Door" + String(doorIndex + 1) + " Angle:" + String(receiveBuffer.angle));
    
  } else if (msgType == "ACK") {
    String fullAction = mapCompactToFull(action);
    sendCompactResponse(doors[doorIndex].serialNumber, fullAction, receiveBuffer.success, action);
    
  } else if (msgType == "STS") {
    doors[doorIndex].doorState = action;
    String fullState = mapStateToFull(action);
    sendCompactStatus(doors[doorIndex].serialNumber, fullState, receiveBuffer.angle);
  }
}

// ✅ OPTIMIZED: Send compact command to ESP-01
void forwardCommandToESP01(String serialNumber, String action) {
  int doorIndex = findDoorBySerial(serialNumber);
  
  if (doorIndex < 0) {
    Serial.println("Door Not Found:" + serialNumber);
    sendErrorResponse(serialNumber, action);
    return;
  }
  
  if (!doors[doorIndex].isOnline) {
    Serial.println("Door" + String(doorIndex + 1) + " Offline");
    sendErrorResponse(serialNumber, action);
    return;
  }
  
  String compactAction = mapActionToCompact(action);
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "CMD", 3);
  strncpy(sendBuffer.action, compactAction.c_str(), 3);
  sendBuffer.angle = 0;
  sendBuffer.success = false;
  sendBuffer.ts = millis();
  
  delay(5);
  int result = esp_now_send(doors[doorIndex].macAddress, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("CMD Sent Door" + String(doorIndex + 1) + ":" + compactAction);
  } else {
    Serial.println("CMD FAIL Door" + String(doorIndex + 1) + ":" + String(result));
    sendErrorResponse(serialNumber, action);
  }
}

// ✅ OPTIMIZED: Compact response format for MEGA
void sendCompactResponse(String serialNumber, String command, bool success, String compactResult) {
  // Create minimal JSON for MEGA
  String json = "{";
  json += "\"s\":" + String(success ? "1" : "0") + ",";  // success -> s
  json += "\"r\":\"" + compactResult + "\",";             // result -> r
  json += "\"d\":\"" + serialNumber + "\",";             // deviceId -> d
  json += "\"c\":\"" + command + "\",";                  // command -> c
  json += "\"a\":" + String(receiveBuffer.angle) + ","; // angle -> a
  json += "\"t\":" + String(millis());                  // timestamp -> t
  json += "}";
  
  Serial.println("RESP:" + json);
}

void sendCompactStatus(String serialNumber, String state, int angle) {
  String json = "{";
  json += "\"d\":\"" + serialNumber + "\",";
  json += "\"s\":\"" + state + "\",";
  json += "\"a\":" + String(angle) + ",";
  json += "\"o\":1,";  // online
  json += "\"t\":" + String(millis());
  json += "}";
  
  Serial.println("STS:" + json);
}

void sendErrorResponse(String serialNumber, String action) {
  String json = "{";
  json += "\"s\":0,";
  json += "\"r\":\"ERROR\",";
  json += "\"d\":\"" + serialNumber + "\",";
  json += "\"c\":\"" + action + "\",";
  json += "\"t\":" + String(millis());
  json += "}";
  
  Serial.println("RESP:" + json);
}

void sendHeartbeatToESP01(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= TOTAL_DOORS) return;
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "HBT", 3);
  strncpy(sendBuffer.action, "PNG", 3);  // "PING" shortened
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  delay(10);
  int result = esp_now_send(doors[doorIndex].macAddress, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  Serial.println("HBT Door" + String(doorIndex + 1) + ":" + String(result == 0 ? "OK" : "FAIL"));
}

void checkESP01Status() {
  unsigned long now = millis();
  
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (doors[i].isOnline && (now - doors[i].lastSeen > 90000)) {  // 90 seconds timeout
      doors[i].isOnline = false;
      Serial.println("Door" + String(i + 1) + " Timeout");
    }
  }
}

void loop() {
  // ✅ OPTIMIZED: Handle MEGA commands with compact parsing
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.startsWith("CMD:")) {
      String cmdData = cmd.substring(4);
      
      int colonIndex = cmdData.indexOf(':');
      if (colonIndex > 0) {
        String serialNumber = cmdData.substring(0, colonIndex);
        String action = cmdData.substring(colonIndex + 1);
        
        Serial.println("RX CMD:" + serialNumber + "->" + action);
        forwardCommandToESP01(serialNumber, action);
      }
    }
  }
  
  // ✅ OPTIMIZED: Faster heartbeat cycle
  static unsigned long lastHeartbeat = 0;
  static int heartbeatIndex = 0;
  
  if (millis() - lastHeartbeat > 8000) {  // Every 8 seconds
    sendHeartbeatToESP01(heartbeatIndex);
    heartbeatIndex = (heartbeatIndex + 1) % TOTAL_DOORS;
    lastHeartbeat = millis();
  }
  
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 20000) {
    checkESP01Status();
    lastStatusCheck = millis();
  }
  
  // Status print
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    int onlineCount = 0;
    for (int i = 0; i < TOTAL_DOORS; i++) {
      if (doors[i].isOnline) onlineCount++;
    }
    Serial.println("STATUS Online:" + String(onlineCount) + "/" + String(TOTAL_DOORS) + 
                   " Heap:" + String(ESP.getFreeHeap()));
    lastStatus = millis();
  }
  
  yield();
  delay(5);
}