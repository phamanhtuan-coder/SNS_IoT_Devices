#define FIRMWARE_VERSION "5.0.0"
#define DEVICE_ID "ESP8266_DOOR_HUB_PCA9685_001"

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <ArduinoJson.h>

// ✅ I2C PCA9685 Servo Controller
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// ✅ Door Configuration (matching Mega device database)
struct DoorConfig {
  String serialNumber;
  uint8_t channel;          // PCA9685 channel (0-15)
  uint16_t minPulse;        // Minimum pulse width (150)
  uint16_t maxPulse;        // Maximum pulse width (600)
  uint16_t closedAngle;     // Closed position angle
  uint16_t openAngle;       // Open position angle
  bool isDualDoor;          // Is this a dual door setup
  uint8_t secondChannel;    // Second servo channel for dual doors
  bool enabled;             // Is this door enabled
  String doorType;          // SERVO_PCA9685, DUAL_PCA9685
};

// ✅ Door Database (matching Mega_Hub_Sensor.ino)
DoorConfig doors[9] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", 0, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", 1, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", 2, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", 3, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", 4, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", 5, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  // Door 7: Dual door setup (channels 6 & 7)
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", 6, 150, 600, 0, 90, true, 7, true, "DUAL_PCA9685"},
  {"SERL27JUN2501JYR2RKVTH6PWR9ETXC2", 8, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVVSBGRTM0TRFW", 9, 150, 600, 0, 90, false, 0, true, "SERVO_PCA9685"}
};

// ✅ Door States
enum DoorState {
  CLOSED = 0,
  OPENING = 1,
  OPEN = 2,
  CLOSING = 3,
  ERROR = 4
};

struct DoorStatus {
  DoorState state;
  uint16_t currentAngle;
  bool isMoving;
  unsigned long lastCommand;
  String lastAction;
  bool online;
};

DoorStatus doorStates[9];

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("ESP8266 Door Hub PCA9685 v5.0.0");
  Serial.println("Device: " + String(DEVICE_ID));
  Serial.println("Controlled by: Arduino Mega via Serial");
  Serial.println("Doors: 9 servos (Door 7 = dual servo)");
  
  // ✅ Initialize I2C and PCA9685
  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(50);  // 50Hz for servos
  delay(100);
  
  // ✅ Initialize all doors to closed position
  for (int i = 0; i < 9; i++) {
    if (doors[i].enabled) {
      moveServoToAngle(i, doors[i].closedAngle);
      doorStates[i] = {CLOSED, doors[i].closedAngle, false, millis(), "INIT", true};
      
      if (doors[i].isDualDoor) {
        moveSecondServoToAngle(i, doors[i].closedAngle);
      }
    }
  }
  
  delay(1000);
  Serial.println("DOOR_HUB_PCA9685_READY");
  Serial.println("CHANNELS:9_doors_initialized");
  
  // Send initial status to Mega
  sendAllDoorStatus();
  
  Serial.println("ESP8266 Door Hub Ready - Awaiting Mega commands");
}

// ✅ Find door by serial number
int findDoorBySerial(String serialNumber) {
  for (int i = 0; i < 9; i++) {
    if (doors[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

// ✅ Door Control Functions
void openDoor(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= 9 || !doors[doorIndex].enabled) return;
  
  if (doorStates[doorIndex].isMoving) {
    Serial.println("[DOOR] " + String(doorIndex + 1) + " already moving");
    return;
  }
  
  Serial.println("[DOOR] Opening door " + String(doorIndex + 1) + " (Ch" + String(doors[doorIndex].channel) + ")");
  
  doorStates[doorIndex].state = OPENING;
  doorStates[doorIndex].isMoving = true;
  doorStates[doorIndex].lastAction = "OPEN";
  doorStates[doorIndex].lastCommand = millis();
  
  // Move primary servo to open position
  moveServoToAngle(doorIndex, doors[doorIndex].openAngle);
  
  // Move second servo if dual door
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].openAngle);
  }
  
  // Simulate movement time
  delay(1000);
  
  doorStates[doorIndex].state = OPEN;
  doorStates[doorIndex].currentAngle = doors[doorIndex].openAngle;
  doorStates[doorIndex].isMoving = false;
  
  sendDoorResponse(doorIndex, "open_door", true);
  sendDoorStatus(doorIndex);
}

void closeDoor(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= 9 || !doors[doorIndex].enabled) return;
  
  if (doorStates[doorIndex].isMoving) {
    Serial.println("[DOOR] " + String(doorIndex + 1) + " already moving");
    return;
  }
  
  Serial.println("[DOOR] Closing door " + String(doorIndex + 1) + " (Ch" + String(doors[doorIndex].channel) + ")");
  
  doorStates[doorIndex].state = CLOSING;
  doorStates[doorIndex].isMoving = true;
  doorStates[doorIndex].lastAction = "CLOSE";
  doorStates[doorIndex].lastCommand = millis();
  
  // Move primary servo to closed position
  moveServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  
  // Move second servo if dual door
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  }
  
  // Simulate movement time
  delay(1000);
  
  doorStates[doorIndex].state = CLOSED;
  doorStates[doorIndex].currentAngle = doors[doorIndex].closedAngle;
  doorStates[doorIndex].isMoving = false;
  
  sendDoorResponse(doorIndex, "close_door", true);
  sendDoorStatus(doorIndex);
}

void toggleDoor(int doorIndex) {
  if (doorStates[doorIndex].state == CLOSED) {
    openDoor(doorIndex);
  } else {
    closeDoor(doorIndex);
  }
}

// ✅ Servo Control Functions
void moveServoToAngle(int doorIndex, uint16_t angle) {
  uint16_t pulse = map(angle, 0, 180, doors[doorIndex].minPulse, doors[doorIndex].maxPulse);
  pwm.setPWM(doors[doorIndex].channel, 0, pulse);
  
  Serial.println("[SERVO] Door " + String(doorIndex + 1) + " Ch" + String(doors[doorIndex].channel) + 
                " -> " + String(angle) + "° (pulse: " + String(pulse) + ")");
}

void moveSecondServoToAngle(int doorIndex, uint16_t angle) {
  if (!doors[doorIndex].isDualDoor) return;
  
  uint16_t pulse = map(angle, 0, 180, doors[doorIndex].minPulse, doors[doorIndex].maxPulse);
  pwm.setPWM(doors[doorIndex].secondChannel, 0, pulse);
  
  Serial.println("[SERVO] Door " + String(doorIndex + 1) + " 2nd Ch" + String(doors[doorIndex].secondChannel) + 
                " -> " + String(angle) + "° (pulse: " + String(pulse) + ")");
}

// ✅ Communication Functions
void sendDoorResponse(int doorIndex, String command, bool success) {
  String json = "{";
  json += "\"s\":" + String(success ? "1" : "0") + ",";
  json += "\"r\":\"" + String(success ? "OK" : "ERR") + "\",";
  json += "\"d\":\"" + doors[doorIndex].serialNumber + "\",";
  json += "\"c\":\"" + command + "\",";
  json += "\"a\":" + String(doorStates[doorIndex].currentAngle) + ",";
  json += "\"ch\":" + String(doors[doorIndex].channel) + ",";
  json += "\"dual\":" + String(doors[doorIndex].isDualDoor ? "1" : "0") + ",";
  json += "\"t\":" + String(millis());
  json += "}";
  
  Serial.println("RESP:" + json);
}

void sendDoorStatus(int doorIndex) {
  String stateStr = "";
  switch(doorStates[doorIndex].state) {
    case CLOSED: stateStr = "closed"; break;
    case OPENING: stateStr = "opening"; break;
    case OPEN: stateStr = "open"; break;
    case CLOSING: stateStr = "closing"; break;
    case ERROR: stateStr = "error"; break;
  }
  
  String json = "{";
  json += "\"d\":\"" + doors[doorIndex].serialNumber + "\",";
  json += "\"s\":\"" + stateStr + "\",";
  json += "\"a\":" + String(doorStates[doorIndex].currentAngle) + ",";
  json += "\"ch\":" + String(doors[doorIndex].channel) + ",";
  json += "\"moving\":" + String(doorStates[doorIndex].isMoving ? "1" : "0") + ",";
  json += "\"dual\":" + String(doors[doorIndex].isDualDoor ? "1" : "0") + ",";
  json += "\"type\":\"" + doors[doorIndex].doorType + "\",";
  json += "\"online\":1,";
  json += "\"t\":" + String(millis());
  json += "}";
  
  Serial.println("STS:" + json);
}

void sendAllDoorStatus() {
  Serial.println("[STATUS] Sending all door status to Mega...");
  for (int i = 0; i < 9; i++) {
    if (doors[i].enabled) {
      sendDoorStatus(i);
      delay(50);
    }
  }
  Serial.println("STATUS_COMPLETE");
}

// ✅ Configuration Functions
void configureDoor(int doorIndex, String configData) {
  // Parse basic config (can be expanded)
  if (configData.indexOf("open_angle") >= 0) {
    int angleStart = configData.indexOf("open_angle") + 11;
    int angleEnd = configData.indexOf(",", angleStart);
    if (angleEnd == -1) angleEnd = configData.length();
    
    String angleStr = configData.substring(angleStart, angleEnd);
    int angle = angleStr.toInt();
    
    if (angle >= 0 && angle <= 180) {
      doors[doorIndex].openAngle = angle;
      Serial.println("[CONFIG] Door " + String(doorIndex + 1) + " open angle: " + String(angle) + "°");
    }
  }
  
  if (configData.indexOf("closed_angle") >= 0) {
    int angleStart = configData.indexOf("closed_angle") + 13;
    int angleEnd = configData.indexOf(",", angleStart);
    if (angleEnd == -1) angleEnd = configData.length();
    
    String angleStr = configData.substring(angleStart, angleEnd);
    int angle = angleStr.toInt();
    
    if (angle >= 0 && angle <= 180) {
      doors[doorIndex].closedAngle = angle;
      Serial.println("[CONFIG] Door " + String(doorIndex + 1) + " closed angle: " + String(angle) + "°");
    }
  }
  
  sendDoorResponse(doorIndex, "configure_door", true);
}

void testDoor(int doorIndex) {
  Serial.println("[TEST] Testing door " + String(doorIndex + 1));
  
  // Test sequence: closed -> open -> closed
  moveServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  }
  delay(1000);
  
  moveServoToAngle(doorIndex, doors[doorIndex].openAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].openAngle);
  }
  delay(1000);
  
  moveServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  if (doors[doorIndex].isDualDoor) {
    moveSecondServoToAngle(doorIndex, doors[doorIndex].closedAngle);
  }
  
  doorStates[doorIndex].state = CLOSED;
  doorStates[doorIndex].currentAngle = doors[doorIndex].closedAngle;
  
  sendDoorResponse(doorIndex, "test_door", true);
  Serial.println("[TEST] Door " + String(doorIndex + 1) + " test complete");
}

// ✅ Command Processing
void handleMegaCommand(String message) {
  if (!message.startsWith("CMD:")) return;
  
  String cmdData = message.substring(4);
  int colonIndex = cmdData.indexOf(':');
  
  if (colonIndex <= 0) {
    Serial.println("[CMD] Invalid format: " + message);
    return;
  }
  
  String serialNumber = cmdData.substring(0, colonIndex);
  String action = cmdData.substring(colonIndex + 1);
  
  int doorIndex = findDoorBySerial(serialNumber);
  
  if (doorIndex < 0) {
    Serial.println("[CMD] Door not found: " + serialNumber);
    return;
  }
  
  Serial.println("[CMD] Door " + String(doorIndex + 1) + " (" + doors[doorIndex].doorType + "): " + action);
  
  doorStates[doorIndex].online = true;
  doorStates[doorIndex].lastCommand = millis();
  
  if (action == "open_door") {
    openDoor(doorIndex);
  } 
  else if (action == "close_door") {
    closeDoor(doorIndex);
  }
  else if (action == "toggle_door") {
    toggleDoor(doorIndex);
  }
  else if (action.startsWith("configure")) {
    configureDoor(doorIndex, action);
  }
  else if (action == "test_door") {
    testDoor(doorIndex);
  }
  else if (action == "get_status") {
    sendDoorStatus(doorIndex);
  }
  else {
    Serial.println("[CMD] Unknown action: " + action);
    sendDoorResponse(doorIndex, action, false);
  }
}

void loop() {
  // Handle commands from Mega via Serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      handleMegaCommand(command);
    }
  }
  
  // Monitor door timeouts
  for (int i = 0; i < 9; i++) {
    if (doorStates[i].isMoving && (millis() - doorStates[i].lastCommand > 5000)) {
      // Movement timeout - stop movement
      doorStates[i].isMoving = false;
      doorStates[i].state = ERROR;
      Serial.println("[TIMEOUT] Door " + String(i + 1) + " movement timeout");
      sendDoorStatus(i);
    }
  }
  
  // Periodic status update
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate > 300000) { // Every 5 minutes
    Serial.println("[HEALTH] ESP8266 Door Hub PCA9685 - " + String(millis() / 1000) + "s uptime");
    Serial.println("         Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("         Doors: 9 servos (Door 7 = dual) via PCA9685");
    lastStatusUpdate = millis();
  }
  
  // Heartbeat to Mega
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) { // Every minute
    Serial.println("[HEARTBEAT] Door Hub PCA9685 operational");
    lastHeartbeat = millis();
  }
  
  delay(50);
}