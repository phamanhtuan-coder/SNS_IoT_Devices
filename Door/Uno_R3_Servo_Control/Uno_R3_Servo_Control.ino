#include <Servo.h>

Servo servos[7];
int doorStates[7] = {0};  // 0=closed, 1=open

void setup() {
  Serial.begin(115200);
  
  // Attach servos to pins 2-8 (for doors 1-7)
  for (int i = 0; i < 7; i++) {
    servos[i].attach(2 + i);
    servos[i].write(0);        // Start at closed position
    doorStates[i] = 0;         // Start closed
  }
  
  Serial.println("UNO Control Ready - 7 Servos on pins 2-8");
}

void loop() {
  // Listen for serial commands from UNO Receive
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.startsWith("SERVO")) {
      int servoNum = cmd.substring(5).toInt();
      controlServo(servoNum);
    }
  }
}

void controlServo(int servoNum) {
  if (servoNum >= 1 && servoNum <= 7) {
    int index = servoNum - 1;  // Convert to 0-based index
    
    // Determine target state based on current state
    int targetState = (doorStates[index] == 0) ? 1 : 0;  // 0->1 or 1->0
    
    // Only move if state actually changes
    if (doorStates[index] != targetState) {
      doorStates[index] = targetState;
      
      // Move servo
      int targetAngle = (targetState == 1) ? 180 : 0;
      servos[index].write(targetAngle);
      
      // Status feedback
      Serial.println("Servo " + String(servoNum) + 
                     " (Pin " + String(2 + index) + ") -> " +
                     (targetState == 1 ? "OPEN (180°)" : "CLOSED (0°)"));
    } else {
      Serial.println("Servo " + String(servoNum) + " already in target state");
    }
  } else {
    Serial.println("Invalid servo number: " + String(servoNum));
  }
}