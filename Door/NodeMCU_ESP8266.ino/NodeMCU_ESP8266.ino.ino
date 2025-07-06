#define FIRMWARE_VERSION "4.0.0"
#define MASTER_ID "ESP_MASTER_DOOR_UNO_001"

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

// ===== ESP MASTER DOOR (ESP8266 + ATmega328P Board) =====
// ESP8266 part: Communicates with MEGA Hub via Serial2
// ATmega328P part: Communicates with Arduino Uno R3
// Inter-communication: ESP8266 ←→ ATmega328P via Serial

// ===== DOOR CONFIGURATION =====
struct DoorConfig {
  String serialNumber;
  int doorId;           // 1-6 for Arduino Uno servos
  bool isOnline;
  unsigned long lastSeen;
  String lastAction;
  int servoAngle;
  String doorState;     // "closed", "opening", "open", "closing"
};

// 6 doors managed by Arduino Uno R3
DoorConfig doors[6] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", 1, false, 0, "none", 0, "closed"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", 2, false, 0, "none", 0, "closed"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", 3, false, 0, "none", 0, "closed"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", 4, false, 0, "none", 0, "closed"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", 5, false, 0, "none", 0, "closed"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", 6, false, 0, "none", 0, "closed"}
};
const int TOTAL_DOORS = 6;

// ===== CONNECTION STATUS =====
bool megaConnected = false;
bool unoConnected = false;
unsigned long lastMegaMessage = 0;
unsigned long lastUnoMessage = 0;

// ===== STATISTICS =====
unsigned long commandsReceived = 0;
unsigned long commandsForwarded = 0;
unsigned long responsesReceived = 0;
unsigned long responsesForwarded = 0;

void setup() {
  Serial.begin(115200);  // Communication with ATmega328P → Arduino Uno
  delay(2000);
  
  Serial.println("\n=== ESP MASTER DOOR (UNO R3) v4.0.0 ===");
  Serial.println("Master ID: " + String(MASTER_ID));
  Serial.println("Managing " + String(TOTAL_DOORS) + " doors via Arduino Uno R3");
  Serial.println("Communication:");
  Serial.println("  - MEGA Hub: Via ATmega328P bridge");
  Serial.println("  - Arduino Uno: Direct Serial");
  
  initializeDoorDatabase();
  
  Serial.println("[INIT] ✓ ESP Master Door Ready");
  Serial.println("Waiting for connections...\n");
}

void initializeDoorDatabase() {
  Serial.println("[INIT] Initializing door database...");
  
  for (int i = 0; i < TOTAL_DOORS; i++) {
    doors[i].isOnline = false;
    doors[i].lastSeen = 0;
    doors[i].lastAction = "none";
    doors[i].servoAngle = 0;
    doors[i].doorState = "closed";
    
    Serial.println("Door " + String(i + 1) + ": " + doors[i].serialNumber + " (Pin " + String(i + 3) + ")");
  }
  
  Serial.println("[INIT] ✓ " + String(TOTAL_DOORS) + " doors configured");
}

void loop() {
  // ===== HANDLE MEGA COMMUNICATION (via ATmega bridge) =====
  // ATmega328P will forward MEGA commands to ESP8266 via Serial
  if (Serial.available()) {
    String message = Serial.readStringUntil('\n');
    handleMessage(message);
  }
  
  // ===== PERIODIC TASKS =====
  static unsigned long lastHealthCheck = 0;
  if (millis() - lastHealthCheck > 30000) {  // Every 30 seconds
    checkConnectionHealth();
    sendStatusUpdate();
    lastHealthCheck = millis();
  }
  
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 120000) {  // Every 2 minutes
    printMasterStatus();
    lastStatusPrint = millis();
  }
  
  // ===== HEARTBEAT TO UNO =====
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 45000) {  // Every 45 seconds
    sendHeartbeatToUno();
    lastHeartbeat = millis();
  }
  
  yield();
  delay(10);
}

void handleMessage(String message) {
  message.trim();
  if (message.length() == 0) return;
  
  if (message.startsWith("MEGA_CMD:")) {
    handleMegaCommand(message);
  } else if (message.startsWith("UNO_RESP:")) {
    handleUnoResponse(message);
  } else if (message.startsWith("UNO_STATUS:")) {
    handleUnoStatus(message);
  } else if (message.startsWith("UNO_HEARTBEAT:")) {
    handleUnoHeartbeat(message);
  } else {
    Serial.println("[MSG] Unknown: " + message);
  }
}

void handleMegaCommand(String cmdMessage) {
  // Format: MEGA_CMD:serialNumber:action
  String cmdData = cmdMessage.substring(9);  // Remove "MEGA_CMD:"
  
  megaConnected = true;
  lastMegaMessage = millis();
  commandsReceived++;
  
  Serial.println("[MEGA→ESP] " + cmdData);
  
  int colonIndex = cmdData.indexOf(':');
  if (colonIndex <= 0) {
    Serial.println("[CMD] ✗ Invalid format: " + cmdMessage);
    sendErrorResponse("unknown", "parse_error", "Invalid command format");
    return;
  }
  
  String serialNumber = cmdData.substring(0, colonIndex);
  String action = cmdData.substring(colonIndex + 1);
  
  int doorIndex = findDoorBySerial(serialNumber);
  if (doorIndex < 0) {
    Serial.println("[CMD] ✗ Door not found: " + serialNumber);
    sendErrorResponse(serialNumber, action, "Door not found");
    return;
  }
  
  // Update door state
  doors[doorIndex].lastAction = action;
  doors[doorIndex].lastSeen = millis();
  
  // Forward to Arduino Uno via Serial
  String unoCommand = "CMD:" + String(doors[doorIndex].doorId) + ":" + action;
  Serial.println(unoCommand);
  
  commandsForwarded++;
  
  Serial.println("[ESP→UNO] " + unoCommand + " (Door " + String(doorIndex + 1) + ")");
}

void handleUnoResponse(String respMessage) {
  // Format: UNO_RESP:doorId:action:success:result:angle
  String respData = respMessage.substring(9);  // Remove "UNO_RESP:"
  
  unoConnected = true;
  lastUnoMessage = millis();
  responsesReceived++;
  
  Serial.println("[UNO→ESP] " + respData);
  
  // Parse response: doorId:action:success:result:angle
  String parts[5];
  int partIndex = 0;
  int startIndex = 0;
  
  for (int i = 0; i <= respData.length() && partIndex < 5; i++) {
    if (i == respData.length() || respData.charAt(i) == ':') {
      parts[partIndex] = respData.substring(startIndex, i);
      startIndex = i + 1;
      partIndex++;
    }
  }
  
  if (partIndex >= 5) {
    int doorId = parts[0].toInt();
    String action = parts[1];
    bool success = (parts[2] == "true");
    String result = parts[3];
    int angle = parts[4].toInt();
    
    // Find door by ID
    int doorIndex = findDoorById(doorId);
    if (doorIndex >= 0) {
      doors[doorIndex].isOnline = true;
      doors[doorIndex].lastSeen = millis();
      doors[doorIndex].servoAngle = angle;
      doors[doorIndex].doorState = result;
      
      Serial.println("[UPDATE] Door " + String(doorIndex + 1) + ": " + action + 
                     " = " + String(success) + " (" + result + ", " + String(angle) + "°)");
      
      // Send response back to MEGA (via ATmega bridge)
      sendResponseToMega(doors[doorIndex].serialNumber, action, success, result, angle);
      responsesForwarded++;
    }
  } else {
    Serial.println("[PARSE] ✗ Invalid UNO response format: " + respMessage);
  }
}

void handleUnoStatus(String statusMessage) {
  // Format: UNO_STATUS:doorId:state:angle:online
  String statusData = statusMessage.substring(11);  // Remove "UNO_STATUS:"
  
  unoConnected = true;
  lastUnoMessage = millis();
  
  Serial.println("[UNO] Status: " + statusData);
  
  // Parse and update door states
  // Implementation for bulk status updates
}

void handleUnoHeartbeat(String heartbeatMessage) {
  unoConnected = true;
  lastUnoMessage = millis();
  
  Serial.println("[UNO] Heartbeat received");
}

void sendResponseToMega(String serialNumber, String action, bool success, String result, int angle) {
  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"result\":\"" + result + "\",";
  json += "\"deviceId\":\"" + serialNumber + "\",";
  json += "\"command\":\"" + action + "\",";
  json += "\"servo_angle\":" + String(angle) + ",";
  json += "\"door_state\":\"" + result + "\",";
  json += "\"uno_processed\":true,";
  json += "\"connection_type\":\"uno_serial\",";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  String response = "MEGA_RESP:" + json;
  Serial.println(response);
  
  Serial.println("[ESP→MEGA] Response sent for " + serialNumber);
}

void sendErrorResponse(String serialNumber, String action, String error) {
  String json = "{";
  json += "\"success\":false,";
  json += "\"result\":\"" + error + "\",";
  json += "\"deviceId\":\"" + serialNumber + "\",";
  json += "\"command\":\"" + action + "\",";
  json += "\"servo_angle\":0,";
  json += "\"uno_processed\":false,";
  json += "\"error\":\"" + error + "\",";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  String response = "MEGA_RESP:" + json;
  Serial.println(response);
}

void sendHeartbeatToUno() {
  Serial.println("HEARTBEAT");
  Serial.println("[ESP→UNO] Heartbeat sent");
}

void sendStatusUpdate() {
  String status = "STATUS_REQUEST";
  Serial.println(status);
  Serial.println("[ESP→UNO] Status request sent");
}

int findDoorBySerial(String serialNumber) {
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (doors[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

int findDoorById(int doorId) {
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (doors[i].doorId == doorId) {
      return i;
    }
  }
  return -1;
}

void checkConnectionHealth() {
  // Check MEGA connection
  if (megaConnected && (millis() - lastMegaMessage > 180000)) {
    megaConnected = false;
    Serial.println("[HEALTH] ✗ MEGA timeout");
  }
  
  // Check UNO connection
  if (unoConnected && (millis() - lastUnoMessage > 120000)) {
    unoConnected = false;
    Serial.println("[HEALTH] ✗ Arduino Uno timeout");
    
    // Mark all doors offline
    for (int i = 0; i < TOTAL_DOORS; i++) {
      doors[i].isOnline = false;
      doors[i].doorState = "offline";
    }
  }
}

void printMasterStatus() {
  Serial.println("\n======= ESP MASTER DOOR STATUS =======");
  Serial.println("Master ID: " + String(MASTER_ID));
  Serial.println("Uptime: " + String(millis() / 1000) + " seconds");
  Serial.println("Commands RX/TX: " + String(commandsReceived) + "/" + String(commandsForwarded));
  Serial.println("Responses RX/TX: " + String(responsesReceived) + "/" + String(responsesForwarded));
  
  Serial.println("\n--- Connections ---");
  Serial.println("MEGA Hub: " + String(megaConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Arduino Uno: " + String(unoConnected ? "CONNECTED" : "DISCONNECTED"));
  
  Serial.println("\n--- Door Status ---");
  int onlineCount = 0;
  for (int i = 0; i < TOTAL_DOORS; i++) {
    if (doors[i].isOnline) onlineCount++;
    
    Serial.println("Door " + String(i + 1) + " (ID:" + String(doors[i].doorId) + "): " +
                   String(doors[i].isOnline ? "ONLINE" : "OFFLINE") + " | " +
                   doors[i].doorState + " | " + String(doors[i].servoAngle) + "° | " +
                   doors[i].serialNumber);
  }
  
  Serial.println("\nOnline Doors: " + String(onlineCount) + "/" + String(TOTAL_DOORS));
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("=====================================\n");
}