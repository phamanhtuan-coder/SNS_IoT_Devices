#include <Servo.h>

Servo servos[7];
int doorStates[7] = {0};  // 0=closed, 1=open

void setup() {
  Serial.begin(115200);
  
  // Attach servos to pins 2-8 (for doors 1-7)
  for (int i = 0; i < 7; i++) {
    servos[i].attach(2 + i);
    doorStates[i] = 0;         // Start closed
  }
  
  Serial.println("UNO Control Ready - 7 Servos on pins 2-8");
  
  // ✅ FORCE RESET - Ensure all servos start at CLOSED position
  Serial.println("FORCE RESET: Moving all servos to CLOSED (0°)");
  for (int i = 0; i < 7; i++) {
    servos[i].write(0);
    delay(100);  // Small delay between servo movements
  }
  Serial.println("RESET COMPLETE: All doors CLOSED");
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
    
    // Door 7 controls TWO servos (dual wing door)
    if (servoNum == 7) {
      // Control servos 6 and 7 together (pins 7 and 8)
      int index1 = 5;  // Servo 6 (pin 7)
      int index2 = 6;  // Servo 7 (pin 8)
      
      // Toggle both door states together
      int targetState = (doorStates[index1] == 0) ? 1 : 0;
      
      if (doorStates[index1] != targetState || doorStates[index2] != targetState) {
        doorStates[index1] = targetState;
        doorStates[index2] = targetState;
        
        // Move both servos
        int targetAngle = (targetState == 1) ? 180 : 0;
        servos[index1].write(targetAngle);
        servos[index2].write(targetAngle);
        
        Serial.println("Door 7 DUAL SERVO - Pins 7&8 -> " +
                       String(targetState == 1 ? "OPEN (180°)" : "CLOSED (0°)"));
      } else {
        Serial.println("Door 7 already in target state");
      }
    } 
    // Single servo doors (1-6)
    else {
      int index = servoNum - 1;
      int targetState = (doorStates[index] == 0) ? 1 : 0;
      
      if (doorStates[index] != targetState) {
        doorStates[index] = targetState;
        
        int targetAngle = (targetState == 1) ? 180 : 0;
        servos[index].write(targetAngle);
        
        Serial.println("Servo " + String(servoNum) + 
                       " (Pin " + String(2 + index) + ") -> " +
                       (targetState == 1 ? "OPEN (180°)" : "CLOSED (0°)"));
      } else {
        Serial.println("Servo " + String(servoNum) + " already in target state");
      }
    }
  } else {
    Serial.println("Invalid servo number: " + String(servoNum));
  }
}