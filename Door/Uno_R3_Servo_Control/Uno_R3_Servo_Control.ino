#include <Servo.h>

// --- Config ---
#define DOOR_COUNT 8
#define SERVO_ANGLE_CLOSED 0
#define SERVO_ANGLE_OPEN   90  // Hoặc 180, tuỳ thực tế servo của bạn

// Mapping các servo PWM theo từng cửa
// Giả sử:
// - D1–D6, D8: 1 servo mỗi cửa
// - D7: 2 servo (servo7A - pin 8, servo7B - pin 10)
const int servoPins[DOOR_COUNT + 1] = {
  2,  // Door 1
  3,  // Door 2
  4,  // Door 3
  5,  // Door 4
  6,  // Door 5
  7,  // Door 6
  8,  // Door 7A
  9,  // Door 8
  10  // Door 7B (cửa đôi)
};

Servo servos[DOOR_COUNT + 1];

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < DOOR_COUNT + 1; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(SERVO_ANGLE_CLOSED);
  }
  Serial.println("UNO_SERVO:READY");
}

void loop() {
  // Đọc từng dòng lệnh từ Serial
  static String inputString = "";
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n' || inChar == '\r') {
      if (inputString.length() > 0) {
        handleCommand(inputString);
        inputString = "";
      }
    } else {
      inputString += inChar;
    }
  }
}

void handleCommand(String cmd) {
  cmd.trim(); // Xoá khoảng trắng thừa
  // Ví dụ: DOOR7:OPEN
  if (cmd.startsWith("DOOR") && (cmd.indexOf(':') != -1)) {
    int colonIdx = cmd.indexOf(':');
    int doorNum = cmd.substring(4, colonIdx).toInt();
    String action = cmd.substring(colonIdx + 1);

    if (doorNum >= 1 && doorNum <= DOOR_COUNT) {
      if (doorNum == 7) {
        // Cửa đôi: Quay cả 2 servo (servo7A: index 6, servo7B: index 8)
        int angle = (action == "OPEN") ? SERVO_ANGLE_OPEN : SERVO_ANGLE_CLOSED;
        servos[6].write(angle);  // servo7A (pin 8)
        servos[8].write(angle);  // servo7B (pin 10)
        Serial.print("DOOR7:");
        Serial.println(action);
      } else {
        int servoIndex = (doorNum > 7) ? doorNum : (doorNum - 1);
        int angle = (action == "OPEN") ? SERVO_ANGLE_OPEN : SERVO_ANGLE_CLOSED;
        servos[servoIndex].write(angle);
        Serial.print("DOOR");
        Serial.print(doorNum);
        Serial.print(":");
        Serial.println(action);
      }
    }
  }
}
