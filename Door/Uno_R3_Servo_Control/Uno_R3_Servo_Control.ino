// UNO R3 Servo Control - Fixed Serial Printing
#include <Servo.h>
#include <EEPROM.h>

#define DOOR_COUNT 8
#define SERVO_ANGLE_CLOSED 0
#define SERVO_ANGLE_OPEN   90

// EEPROM configuration
#define EEPROM_START_ADDR 0
#define EEPROM_INIT_FLAG 100
#define EEPROM_MAGIC_VALUE 0xAA

// Servo pin mapping (Door 7 dual wing + Door 8 corrected)
const int servoPins[DOOR_COUNT + 1] = {
  2,  // Door 1
  3,  // Door 2
  4,  // Door 3
  5,  // Door 4
  6,  // Door 5
  7,  // Door 6
  8,  // Door 7A (dual wing)
  10, // Door 8 (corrected pin)
  9   // Door 7B (dual wing)
};

Servo servos[DOOR_COUNT + 1];
bool doorStates[DOOR_COUNT + 1] = {false};
unsigned long lastCommandTime[DOOR_COUNT + 1] = {0};
unsigned long lastStatusPrint = 0;
unsigned long commandsProcessed = 0;

String inputString = "";
bool stringComplete = false;
const unsigned long COMMAND_COOLDOWN = 500;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("UNO_SERVO:INIT");
  delay(10);
  
  // Initialize servos
  for (int i = 0; i < DOOR_COUNT + 1; i++) {
    servos[i].attach(servoPins[i]);
    Serial.print("SERVO");
    Serial.print(i + 1);
    Serial.print(":PIN");
    Serial.print(servoPins[i]);
    Serial.println(":ATTACHED");
    delay(50);
  }
  
  // Load states from EEPROM
  loadDoorStates();
  
  // Set initial positions
  for (int i = 0; i < DOOR_COUNT + 1; i++) {
    int angle = doorStates[i] ? SERVO_ANGLE_OPEN : SERVO_ANGLE_CLOSED;
    servos[i].write(angle);
    Serial.print("SERVO");
    Serial.print(i + 1);
    Serial.print(":ANGLE:");
    Serial.print(angle);
    Serial.println("°");
    delay(100);
  }
  
  Serial.println("UNO_SERVO:READY");
  delay(10);
  Serial.print("DOORS:");
  Serial.println(DOOR_COUNT);
  delay(10);
  
  printAllDoorStates();
}

void loop() {
  // ===== ENHANCED SERIAL PROCESSING WITH BUFFER PROTECTION =====
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    
    // Buffer overflow protection
    if (inputString.length() >= 50) {
      inputString = "";  // Reset if too long
      Serial.println("BUFFER_RESET");
      continue;
    }
    
    if (inChar == '\n' || inChar == '\r') {
      if (inputString.length() > 0 && inputString.length() <= 20) {
        stringComplete = true;
      } else {
        inputString = "";
      }
    } else if (inChar >= 32 && inChar <= 126) {
      inputString += inChar;
    }
  }
  
  if (stringComplete) {
    // Process command
    if (inputString.indexOf("ESP01_RX:") == 0) {
      String cleanCommand = inputString.substring(8);
      Serial.print("FWD:");
      Serial.println(cleanCommand);
      handleCommand(cleanCommand);
    } else {
      handleCommand(inputString);
    }
    inputString = "";
    stringComplete = false;
  }
  
  // Status reporting (reduced frequency)
  if (millis() - lastStatusPrint > 60000) {  // Every minute instead of 30s
    printSimpleStatus();  // Use simplified status instead
    lastStatusPrint = millis();
  }
  
  delay(10);
}

void handleCommand(String cmd) {
  cmd.trim();
  
  // Filter system messages
  if (cmd.startsWith("ACK:") || 
      cmd.startsWith("UNO_RX:") || 
      cmd.startsWith("DirectCmd:") || 
      cmd.indexOf("PULSE") != -1 ||
      cmd.indexOf("DURATION") != -1 ||
      cmd == "") {
    return;
  }
  
  commandsProcessed++;
  
  // Handle door commands: D1:1, D2:0, etc.
  if (cmd.startsWith("D") && cmd.indexOf(':') != -1) {
    int colonIdx = cmd.indexOf(':');
    String doorPart = cmd.substring(1, colonIdx);
    String statePart = cmd.substring(colonIdx + 1);
    
    int doorNum = doorPart.toInt();
    int state = statePart.toInt();
    
    if (doorNum >= 1 && doorNum <= DOOR_COUNT && (state == 0 || state == 1)) {
      Serial.print("PROC:D");
      Serial.print(doorNum);
      Serial.print(":");
      Serial.println(state);
      controlDoor(doorNum, state == 1);
    } else {
      Serial.println("INVALID_DOOR");
    }
  }
  // System commands
  else if (cmd == "TEST") {
    runTestSequence();
  }
  else if (cmd == "STATUS") {
    printSimpleStatus();
  }
  else if (cmd == "RESET") {
    resetAllDoors();
  }
}

void controlDoor(int doorNum, bool openState) {
  if (doorNum < 1 || doorNum > DOOR_COUNT) {
    Serial.println("ERROR:INVALID_DOOR");
    return;
  }
  
  unsigned long currentTime = millis();
  
  // Cooldown check
  if (currentTime - lastCommandTime[doorNum] < COMMAND_COOLDOWN) {
    Serial.println("COOLDOWN:D" + String(doorNum));
    return;
  }
  
  bool wasOpen = doorStates[doorNum];
  doorStates[doorNum] = openState;
  
  int angle = openState ? SERVO_ANGLE_OPEN : SERVO_ANGLE_CLOSED;
  
  if (doorNum == 7) {
    // Door 7: Dual wing control (both servos)
    servos[6].write(angle);  // servo7A (index 6, pin 8)
    servos[8].write(angle);  // servo7B (index 8, pin 9)
    Serial.print("D7:");
    Serial.println(openState ? "OPEN" : "CLOSE");
  } else {
    // Single door control
    int servoIndex = (doorNum > 7) ? doorNum : (doorNum - 1);
    servos[servoIndex].write(angle);
    Serial.print("D");
    Serial.print(doorNum);
    Serial.print(":");
    Serial.println(openState ? "OPEN" : "CLOSE");
  }
  
  lastCommandTime[doorNum] = currentTime;
  
  // Save state if changed
  if (wasOpen != openState) {
    saveDoorStates();
    Serial.print("SAVED:D");
    Serial.print(doorNum);
    Serial.print(":");
    Serial.println(openState ? "O" : "C");
  }
  
  // Send acknowledgment (simplified)
  Serial.print("ACK:D");
  Serial.print(doorNum);
  Serial.print(":");
  Serial.println(openState ? "1" : "0");
}

void printAllDoorStates() {
  Serial.print("STATE:");
  for (int i = 1; i <= DOOR_COUNT; i++) {
    Serial.print("D");
    Serial.print(i);
    Serial.print(":");
    Serial.print(doorStates[i] ? "O" : "C");
    if (i < DOOR_COUNT) Serial.print(",");
  }
  Serial.println();
}

// Simplified status to avoid memory issues
void printSimpleStatus() {
  Serial.println("-- STATUS --");
  delay(5);
  Serial.print("CMD_COUNT:");
  Serial.println(commandsProcessed);
  delay(5);
  
  printAllDoorStates();
  delay(5);
  
  Serial.println("-- END --");
}

void resetAllDoors() {
  Serial.println("RESET:ALL");
  for (int i = 1; i <= DOOR_COUNT; i++) {
    lastCommandTime[i] = 0; // Reset cooldown
    controlDoor(i, false);
    delay(200);
  }
  Serial.println("RESET:DONE");
}

void runTestSequence() {
  Serial.println("TEST:START");
  
  for (int i = 1; i <= DOOR_COUNT; i++) {
    Serial.print("TEST:D");
    Serial.println(i);
    
    lastCommandTime[i] = 0;
    controlDoor(i, true);   // Open
    delay(1500);
    
    lastCommandTime[i] = 0;
    controlDoor(i, false);  // Close
    delay(500);
  }
  Serial.println("TEST:DONE");
}

void saveDoorStates() {
  EEPROM.update(EEPROM_INIT_FLAG, EEPROM_MAGIC_VALUE);
  
  for (int i = 1; i <= DOOR_COUNT; i++) {
    EEPROM.update(EEPROM_START_ADDR + i, doorStates[i] ? 1 : 0);
  }
  
  Serial.println("EEPROM:SAVED");
}

void loadDoorStates() {
  uint8_t initFlag = EEPROM.read(EEPROM_INIT_FLAG);
  
  if (initFlag == EEPROM_MAGIC_VALUE) {
    for (int i = 1; i <= DOOR_COUNT; i++) {
      uint8_t state = EEPROM.read(EEPROM_START_ADDR + i);
      doorStates[i] = (state == 1);
    }
    Serial.println("EEPROM:LOADED");
  } else {
    for (int i = 1; i <= DOOR_COUNT; i++) {
      doorStates[i] = false;
    }
    saveDoorStates();
    Serial.println("EEPROM:INIT");
  }
}