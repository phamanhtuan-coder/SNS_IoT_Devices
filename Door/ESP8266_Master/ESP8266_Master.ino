#define FIRMWARE_VERSION "4.2.1"
#define GATEWAY_ID "ESP_GATEWAY_002"

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ArduinoJson.h>

// ✅ OPTIMIZED: Compact message structure matching ESP-01 and ESP32
struct CompactMessage {
  char type[4];        // "CMD", "ACK", "HBT", "STS" - NULL terminated
  char action[4];      // "OPN", "CLS", "TGL", "ALV", etc. - NULL terminated
  int angle;           // Servo angle
  bool success;        // Success flag
  unsigned long ts;    // Timestamp
} __attribute__((packed)); // Ensure consistent packing

// ✅ UPDATED: Extended door configuration for 10 doors
struct DoorConfig {
  String serialNumber;
  uint8_t macAddress[6];
  int doorId;
  String doorType;     // "SERVO", "ROLLING", "SLIDING"
  bool isOnline;
  unsigned long lastSeen;
  int currentAngle;
  String doorState;
};

DoorConfig doors[10] = {
  // Original 7 ESP-01 servo doors
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", {0x84, 0x0D, 0x8E, 0xA4, 0x91, 0x58}, 1, "SERVO", false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", {0x84, 0x0D, 0x8E, 0xA4, 0x3b, 0xe0}, 2, "SERVO", false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", {0x3c, 0x71, 0xbf, 0x39, 0x31, 0x2a}, 3, "SERVO", false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0x9c}, 4, "SERVO", false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", {0x84, 0x0d, 0x8e, 0xa4, 0x91, 0xa4}, 5, "SERVO", false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", {0x84, 0x0d, 0x8e, 0xa4, 0x3a, 0xd2}, 6, "SERVO", false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", {0x84, 0x0d, 0x8e, 0xa4, 0x3b, 0x29}, 7, "SERVO", false, 0, 0, "CLD"},
  {"SERL27JUN2501JYR2RKVTH6PWR9ETXC2", {0x3c, 0x71, 0xbf, 0x39, 0x35, 0x47}, 8, "SERVO", false, 0, 0, "CLD"},  
  // Door ID 9: ESP32 rolling door 
  {"SERL27JUN2501JYR2RKVVSBGRTM0TRFW", {0xb0, 0xb2, 0x1c, 0x97, 0xc6, 0xd0}, 9, "ROLLING", false, 0, 0, "CLD"},
  // Door ID 10: Sliding door - NEED ACTUAL MAC ADDRESS
  {"SERL27JUN2501JYR2RKVSQGM7E9S9D9A", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 10, "SLIDING", false, 0, 0, "CLD"}
};
const int TOTAL_DOORS = 10;  // Only count active doors (1-7 + 9), skip 8 and 10 for now

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
  
  Serial.println("ESP Gateway v4.2.1 - FIXED");
  Serial.println("ID:" + String(GATEWAY_ID));
  Serial.println("Active Doors:" + String(TOTAL_DOORS) + " (7 Servo + 1 Rolling + 1 Sliding)");
  Serial.println("Door ID 9: Rolling (ESP32) - Active");
  Serial.println("Door ID 10: Sliding (ESP8266) - Need MAC");
  Serial.println("MAC:" + WiFi.macAddress());
  
  setupESPNow();
  
  // Initialize buffers
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  
  Serial.println("Gateway Ready with Mixed Door Types");
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
  
  // Add peers for active doors only (skip doors with empty MAC)
  int peersAdded = 0;
  for (int i = 0; i < 10; i++) {  // Check all 10 door slots
    // Skip doors with empty MAC address or empty serial
    bool hasValidMAC = false;
    for (int j = 0; j < 6; j++) {
      if (doors[i].macAddress[j] != 0x00) {
        hasValidMAC = true;
        break;
      }
    }
    
    if (hasValidMAC && doors[i].serialNumber.length() > 0) {
      int result = esp_now_add_peer(doors[i].macAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
      if (result == 0) {
        Serial.println("Door" + String(doors[i].doorId) + " (" + doors[i].doorType + ") Peer OK");
        peersAdded++;
      } else {
        Serial.println("Door" + String(doors[i].doorId) + " (" + doors[i].doorType + ") Peer FAIL:" + String(result));
      }
    } else {
      Serial.println("Door" + String(doors[i].doorId) + " (" + doors[i].doorType + ") Skipped (No MAC/Serial)");
    }
  }
  Serial.println("Total peers added: " + String(peersAdded));
}

int findDoorByMAC(uint8_t *mac_addr) {
  for (int i = 0; i < 10; i++) {  // Check all 10 door slots
    if (memcmp(mac_addr, doors[i].macAddress, 6) == 0) {
      return i;
    }
  }
  return -1;
}

int findDoorBySerial(String serialNumber) {
  for (int i = 0; i < 10; i++) {  // Check all 10 door slots
    if (doors[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  int doorIndex = findDoorByMAC(mac_addr);
  String doorInfo = (doorIndex >= 0) ? 
    ("Door" + String(doors[doorIndex].doorId) + "(" + doors[doorIndex].doorType + ")") : 
    "Unknown";
  
  if (sendStatus == 0) {
    Serial.println("TX OK " + doorInfo);
  } else {
    Serial.println("TX FAIL " + doorInfo + ":" + String(sendStatus));
    
    // ✅ FIXED: Mark door offline on persistent TX failures
    if (doorIndex >= 0) {
      doors[doorIndex].isOnline = false;
    }
  }
}

void onDataReceived(uint8_t *mac_addr, uint8_t *data, uint8_t len) {
  int doorIndex = findDoorByMAC(mac_addr);
  
  // ✅ FIXED: Enhanced validation
  if (doorIndex < 0) {
    Serial.println("RX Unknown MAC");
    return;
  }
  
  if (len != sizeof(CompactMessage)) {
    Serial.println("RX Invalid Size:" + String(len) + " expected:" + String(sizeof(CompactMessage)));
    return;
  }
  
  // ✅ FIXED: Safe memory handling
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  memcpy(&receiveBuffer, data, len);
  
  // ✅ FIXED: Ensure null termination
  receiveBuffer.type[3] = '\0';
  receiveBuffer.action[3] = '\0';
  
  doors[doorIndex].lastSeen = millis();
  doors[doorIndex].isOnline = true;
  doors[doorIndex].currentAngle = receiveBuffer.angle;
  
  String msgType = String(receiveBuffer.type);
  String action = String(receiveBuffer.action);
  String doorInfo = "Door" + String(doors[doorIndex].doorId) + "(" + doors[doorIndex].doorType + ")";
  
  Serial.println("RX " + doorInfo + ":" + msgType + ":" + action);
  
  if (msgType == "HBT") {
    // Handle alive messages from ESP-01
    if (action == "ALV") {
      Serial.println("ALIVE " + doorInfo + " Angle:" + String(receiveBuffer.angle));
      doors[doorIndex].isOnline = true;
    } else {
      Serial.println("HBT " + doorInfo + " Angle:" + String(receiveBuffer.angle));
    }
    
  } else if (msgType == "ACK") {
    String fullAction = mapCompactToFull(action);
    sendCompactResponse(doors[doorIndex].serialNumber, fullAction, receiveBuffer.success, action, doors[doorIndex].doorType);
    
  } else if (msgType == "STS") {
    doors[doorIndex].doorState = action;
    String fullState = mapStateToFull(action);
    sendCompactStatus(doors[doorIndex].serialNumber, fullState, receiveBuffer.angle, doors[doorIndex].doorType);
    
  } else if (msgType == "CFG") {
    // Handle configuration messages from doors
    String doorType = doors[doorIndex].doorType;
    if (action == "ANG" && doorType == "SERVO") {
      // ESP-01 servo angles: closed_angle << 8 | open_angle
      int closedAngle = (receiveBuffer.angle >> 8) & 0xFF;
      int openAngle = receiveBuffer.angle & 0xFF;
      sendConfigResponse(doors[doorIndex].serialNumber, "servo_angles", closedAngle, openAngle, doorType);
    } else if (action == "RND" && doorType == "ROLLING") {
      // ESP32 rolling rounds: closed_rounds << 8 | open_rounds  
      int closedRounds = (receiveBuffer.angle >> 8) & 0xFF;
      int openRounds = receiveBuffer.angle & 0xFF;
      sendConfigResponse(doors[doorIndex].serialNumber, "rolling_rounds", closedRounds, openRounds, doorType);
    } else if (action == "SLD" && doorType == "SLIDING") {
      // ESP32 sliding config: pir_enabled | closed_rounds << 8 | open_rounds
      bool pirEnabled = (receiveBuffer.angle & 0x8000) != 0;
      int closedRounds = (receiveBuffer.angle >> 8) & 0x7F;
      int openRounds = receiveBuffer.angle & 0xFF;
      sendSlidingConfigResponse(doors[doorIndex].serialNumber, closedRounds, openRounds, pirEnabled);
    }
  }
}

// ✅ OPTIMIZED: Send compact command to door (ESP-01 or ESP32)
void forwardCommandToDoor(String serialNumber, String action) {
  int doorIndex = findDoorBySerial(serialNumber);
  
  if (doorIndex < 0) {
    Serial.println("Door Not Found:" + serialNumber);
    sendErrorResponse(serialNumber, action);
    return;
  }
  
  // Check if door has valid MAC address
  bool hasValidMAC = false;
  for (int j = 0; j < 6; j++) {
    if (doors[doorIndex].macAddress[j] != 0x00) {
      hasValidMAC = true;
      break;
    }
  }
  
  if (!hasValidMAC) {
    Serial.println("Door" + String(doors[doorIndex].doorId) + " (" + doors[doorIndex].doorType + ") No MAC Address");
    sendErrorResponse(serialNumber, action);
    return;
  }
  
  // ✅ REMOVED: Don't check isOnline for commands, let them try
  String compactAction = mapActionToCompact(action);
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  // ✅ FIXED: Safe string copying
  strncpy(sendBuffer.type, "CMD", 3);
  sendBuffer.type[3] = '\0';
  
  strncpy(sendBuffer.action, compactAction.c_str(), 3);
  sendBuffer.action[3] = '\0';
  
  sendBuffer.angle = 0;
  sendBuffer.success = false;
  sendBuffer.ts = millis();
  
  delay(5);
  int result = esp_now_send(doors[doorIndex].macAddress, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("CMD Sent Door" + String(doors[doorIndex].doorId) + " (" + doors[doorIndex].doorType + "):" + compactAction);
  } else {
    Serial.println("CMD FAIL Door" + String(doors[doorIndex].doorId) + " (" + doors[doorIndex].doorType + "):" + String(result));
    sendErrorResponse(serialNumber, action);
  }
}

// ✅ OPTIMIZED: Compact response format for MEGA with door type info
void sendCompactResponse(String serialNumber, String command, bool success, String compactResult, String doorType) {
  // Create minimal JSON for MEGA
  String json = "{";
  json += "\"s\":" + String(success ? "1" : "0") + ",";  // success -> s
  json += "\"r\":\"" + compactResult + "\",";             // result -> r
  json += "\"d\":\"" + serialNumber + "\",";             // deviceId -> d
  json += "\"c\":\"" + command + "\",";                  // command -> c
  json += "\"a\":" + String(receiveBuffer.angle) + ","; // angle -> a
  json += "\"dt\":\"" + doorType + "\",";                // doorType -> dt
  json += "\"t\":" + String(millis());                  // timestamp -> t
  json += "}";
  
  Serial.println("RESP:" + json);
}

void sendCompactStatus(String serialNumber, String state, int angle, String doorType) {
  String json = "{";
  json += "\"d\":\"" + serialNumber + "\",";
  json += "\"s\":\"" + state + "\",";
  json += "\"a\":" + String(angle) + ",";
  json += "\"dt\":\"" + doorType + "\",";
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

void sendConfigResponse(String serialNumber, String configType, int value1, int value2, String doorType) {
  String json = "{";
  json += "\"d\":\"" + serialNumber + "\",";
  json += "\"type\":\"" + configType + "\",";
  json += "\"v1\":" + String(value1) + ",";
  json += "\"v2\":" + String(value2) + ",";
  json += "\"dt\":\"" + doorType + "\",";
  json += "\"t\":" + String(millis());
  json += "}";
  
  Serial.println("CFG:" + json);
}

void sendSlidingConfigResponse(String serialNumber, int closedRounds, int openRounds, bool pirEnabled) {
  String json = "{";
  json += "\"d\":\"" + serialNumber + "\",";
  json += "\"type\":\"sliding_config\",";
  json += "\"closed_rounds\":" + String(closedRounds) + ",";
  json += "\"open_rounds\":" + String(openRounds) + ",";
  json += "\"pir_enabled\":" + String(pirEnabled ? "true" : "false") + ",";
  json += "\"dt\":\"SLIDING\",";
  json += "\"t\":" + String(millis());
  json += "}";
  
  Serial.println("CFG:" + json);
}

void sendHeartbeatToDoor(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= 10) return;
  
  // Skip doors without valid MAC or serial
  bool hasValidMAC = false;
  for (int j = 0; j < 6; j++) {
    if (doors[doorIndex].macAddress[j] != 0x00) {
      hasValidMAC = true;
      break;
    }
  }
  
  if (!hasValidMAC || doors[doorIndex].serialNumber.length() == 0) {
    return;  // Skip heartbeat for inactive doors
  }
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  // ✅ FIXED: Safe string copying
  strncpy(sendBuffer.type, "HBT", 3);
  sendBuffer.type[3] = '\0';
  
  strncpy(sendBuffer.action, "PNG", 3);  // "PING" shortened
  sendBuffer.action[3] = '\0';
  
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  delay(10);
  int result = esp_now_send(doors[doorIndex].macAddress, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  String doorInfo = "Door" + String(doors[doorIndex].doorId) + "(" + doors[doorIndex].doorType + ")";
  Serial.println("HBT " + doorInfo + ":" + String(result == 0 ? "OK" : "FAIL"));
}

void checkDoorStatus() {
  unsigned long now = millis();
  
  for (int i = 0; i < 10; i++) {  // Check all 10 door slots
    if (doors[i].isOnline && (now - doors[i].lastSeen > 90000)) {  // 90 seconds timeout
      doors[i].isOnline = false;
      String doorInfo = "Door" + String(doors[i].doorId) + "(" + doors[i].doorType + ")";
      Serial.println(doorInfo + " Timeout");
    }
  }
}

// ✅ FIXED: Only heartbeat active doors with valid MAC addresses
void loop() {
  // Handle MEGA commands
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
        forwardCommandToDoor(serialNumber, action);
      }
    }
  }
  
  // ✅ FIXED: Heartbeat only doors with valid MAC addresses
  static unsigned long lastHeartbeat = 0;
  static int heartbeatIndex = 0;
  // All active door indices including rolling door: 0,1,2,3,4,5,6,7,8 (Door 9 now has MAC)
  static int activeDoorIndices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  static int numActiveDoors = 9;
  
  if (millis() - lastHeartbeat > 8000) {  // Every 8 seconds
    sendHeartbeatToDoor(activeDoorIndices[heartbeatIndex]);
    heartbeatIndex = (heartbeatIndex + 1) % numActiveDoors;
    lastHeartbeat = millis();
  }
  
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 20000) {
    checkDoorStatus();
    lastStatusCheck = millis();
  }
  
  // Status print
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    int onlineCount = 0;
    for (int i = 0; i < 10; i++) {
      if (doors[i].isOnline) onlineCount++;
    }
    Serial.println("STATUS Online:" + String(onlineCount) + "/9 active doors" +
                   " Heap:" + String(ESP.getFreeHeap()));
    lastStatus = millis();
  }
  
  yield();
  delay(5);
}