// UNO Signal Receiver - SoftwareSerial Mode
#include <SoftwareSerial.h>

#define ESP01_RX_PIN 2  // Connect to ESP01's TX (GPIO2)
#define NUM_DOORS 8

// Create SoftwareSerial instance to communicate with ESP01
SoftwareSerial espSerial(ESP01_RX_PIN, -1); // RX only, no TX needed

bool doorStates[NUM_DOORS] = {false};
unsigned long lastCommandTime[NUM_DOORS] = {0};
unsigned long lastDebugPrint = 0;
unsigned long lastCheck = 0;

const unsigned long commandCooldown = 1000;
const unsigned long debugInterval = 15000;

void setup() {
  Serial.begin(115200);   // Communication with Servo Controller UNO
  espSerial.begin(9600);  // Communication with ESP01
  
  // Initialize pins for monitoring
  pinMode(ESP01_RX_PIN, INPUT_PULLUP);
  
  Serial.println("UNO_RX:READY_SOFTWARESERIAL_MODE");
  Serial.println("ESP01 RX Pin: " + String(ESP01_RX_PIN));
  
  delay(100);
  printDoorStates("INIT");
}

void loop() {
  // Check for commands from ESP01 via SoftwareSerial
  if (espSerial.available()) {
    String cmd = espSerial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.length() > 0) {
      Serial.println("ESP01_RX:" + cmd);
      processCommand(cmd);
    }
  }
  
  // Check pin states periodically
  if (millis() - lastCheck > 500) {
    monitorPins();
    lastCheck = millis();
  }
  
  // Debug output
  if (millis() - lastDebugPrint > debugInterval) {
    printDoorStates("STATUS");
    lastDebugPrint = millis();
  }
  
  processSerial();
}

void processCommand(String cmd) {
  // Only process valid formatted commands (D1:1, D8:0, etc.)
  if (cmd.startsWith("D") && cmd.indexOf(':') != -1) {
    int colonPos = cmd.indexOf(':');
    String doorPart = cmd.substring(1, colonPos);
    String statePart = cmd.substring(colonPos + 1);
    
    int doorNum = doorPart.toInt();
    int state = statePart.toInt();
    
    if (doorNum >= 1 && doorNum <= NUM_DOORS && (state == 0 || state == 1)) {
      // Command cooldown check
      if (millis() - lastCommandTime[doorNum-1] < commandCooldown) {
        Serial.println("D" + String(doorNum) + ":COOLDOWN");
        return;
      }
      
      // Update state and forward to servo controller
      doorStates[doorNum-1] = (state == 1);
      lastCommandTime[doorNum-1] = millis();
      
      // Forward command to servo controller
      Serial.println(cmd);
      
      // Log the action
      String action = (state == 1) ? "OPEN" : "CLOSE";
      Serial.println("DOOR" + String(doorNum) + ":" + action + ":" + 
                    (state == 1 ? "OPEN" : "CLOSED") + " [SERIAL_CMD]");
    }
  }
}

void monitorPins() {
  // Monitor ESP01_RX_PIN for debug purposes
  bool pinState = digitalRead(ESP01_RX_PIN);
  static bool lastPinState = HIGH;
  
  if (pinState != lastPinState) {
    Serial.println("ESP01_PIN:" + String(pinState ? "HIGH" : "LOW"));
    lastPinState = pinState;
  }
}

void printDoorStates(String label) {
  Serial.print(label + ":");
  for (int i = 0; i < NUM_DOORS; i++) {
    Serial.print("D" + String(i + 1) + ":");
    Serial.print(doorStates[i] ? "O" : "C");
    if (i < NUM_DOORS - 1) Serial.print(",");
  }
  Serial.println();
}

void processSerial() {
  static String inputString = "";
  
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n') {
      processSerialCommand(inputString);
      inputString = "";
    } else {
      inputString += inChar;
    }
  }
}

void processSerialCommand(String cmd) {
  cmd.trim();
  
  // Filter system messages
  if (cmd.startsWith("ACK:") || cmd.startsWith("UNO_SERVO:") || 
      cmd.startsWith("DirectCmd:") || cmd.startsWith("RESET:") || cmd == "") {
    return;
  }
  
  if (cmd == "DEBUG") {
    printDoorStates("DEBUG");
    for (int i = 0; i < NUM_DOORS; i++) {
      Serial.println("D" + String(i + 1) + "_STATE:" + 
                    String(doorStates[i] ? "OPEN" : "CLOSED") + 
                    "|LAST_CMD:" + String((millis() - lastCommandTime[i]) / 1000) + "s_ago");
    }
  }
  else if (cmd == "RESET") {
    resetAllDoors();
  }
  else if (cmd == "TEST") {
    runTestSequence();
  }
}

void resetAllDoors() {
  Serial.println("RESET:ALL_DOORS");
  for (int i = 0; i < NUM_DOORS; i++) {
    doorStates[i] = false;
    lastCommandTime[i] = 0;
    Serial.println("D" + String(i + 1) + ":0");
  }
}

void runTestSequence() {
  Serial.println("TEST:SERIAL_SEQUENCE");
  for (int i = 1; i <= NUM_DOORS; i++) {
    Serial.println("D" + String(i) + ":1");
    delay(1500);
    Serial.println("D" + String(i) + ":0");
    delay(1000);
  }
  Serial.println("TEST:COMPLETE");
}