#include <SPI.h>
#include <MFRC522.h>
#include <Stepper.h>

// Định nghĩa chân cho RFID
#define SS_PIN 21  // SDA cho ESP32
#define RST_PIN 22 // RST cho ESP32

// Số bước mỗi vòng cho motor 28BYJ-48
const int STEPS_PER_REVOLUTION = 2048;
const int STEPS_FOR_2_5_REVOLUTIONS = STEPS_PER_REVOLUTION * 2.3; // 5120 bước (2.3 vòng)

// Chân nút nhấn
const int buttonCCWPin = 25;  // Nút đóng cửa (quay ngược - CCW, phải qua trái)
const int buttonCWPin = 26;   // Nút mở cửa (quay thuận - CW, trái qua phải)

// Khởi tạo motor stepper (IN1, IN3, IN2, IN4)
Stepper myStepper(STEPS_PER_REVOLUTION, 16, 14, 15, 13); // Đổi thứ tự chân để đảo ngược hướng quay

// Khởi tạo RFID
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Biến trạng thái
int cwButtonState = HIGH;
int ccwButtonState = HIGH;
int lastCWButtonState = HIGH;
int lastCCWButtonState = HIGH;
bool motorRunning = false;
bool isDoorOpen = false; // Trạng thái cửa: false = đóng, true = mở

// Chống dội cho nút nhấn
unsigned long lastCWDebounceTime = 0;
unsigned long lastCCWDebounceTime = 0;
const unsigned long debounceDelay = 50; // 50ms để chống nhiễu

void setup() 
{
  Serial.begin(115200);  // Baud rate cao cho ESP32
  SPI.begin();           // Khởi tạo SPI bus
  mfrc522.PCD_Init();    // Khởi tạo MFRC522
  Serial.println("Approximate your card to the reader...");
  Serial.println();

  // Thiết lập tốc độ motor
  myStepper.setSpeed(10); // Tốc độ 10 RPM

  // Thiết lập chân nút nhấn
  pinMode(buttonCWPin, INPUT_PULLUP);   // GPIO 26: Mở cửa (CW)
  pinMode(buttonCCWPin, INPUT_PULLUP);  // GPIO 25: Đóng cửa (CCW)

  // Thiết lập chân motor
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);

  // Đọc trạng thái ban đầu của nút nhấn
  cwButtonState = digitalRead(buttonCWPin);
  ccwButtonState = digitalRead(buttonCCWPin);
  lastCWButtonState = cwButtonState;
  lastCCWButtonState = ccwButtonState;

  // Đảm bảo cửa ở trạng thái đóng ban đầu
  isDoorOpen = false;

  // Đợi 1 giây để tín hiệu ổn định
  delay(1000);
}

void turnOffMotor() {
  // Tắt tất cả các chân điều khiển motor
  digitalWrite(13, LOW);
  digitalWrite(14, LOW);
  digitalWrite(15, LOW);
  digitalWrite(16, LOW);
}

void loop() 
{
  // Xử lý nút nhấn nếu motor không chạy
  if (!motorRunning) {
    int cwReading = digitalRead(buttonCWPin);    // GPIO 26: Mở cửa (CW)
    int ccwReading = digitalRead(buttonCCWPin);  // GPIO 25: Đóng cửa (CCW)

    // Xử lý nút mở cửa (CW, trái qua phải)
    if (cwReading != lastCWButtonState) {
      lastCWDebounceTime = millis();
    }
    if ((millis() - lastCWDebounceTime) > debounceDelay) {
      if (cwReading != cwButtonState) {
        cwButtonState = cwReading;
        if (cwButtonState == LOW && !isDoorOpen) { // Chỉ mở nếu cửa chưa mở
          motorRunning = true;
          Serial.println("Manual Open: Button CW pressed");
          myStepper.step(STEPS_FOR_2_5_REVOLUTIONS); // Quay thuận 2.3 vòng (trái qua phải)
          turnOffMotor(); // Tắt motor
          isDoorOpen = true;
          motorRunning = false;
        }
      }
    }
    lastCWButtonState = cwReading;

    // Xử lý nút đóng cửa (CCW, phải qua trái)
    if (ccwReading != lastCCWButtonState) {
      lastCCWDebounceTime = millis();
    }
    if ((millis() - lastCCWDebounceTime) > debounceDelay) {
      if (ccwReading != ccwButtonState) {
        ccwButtonState = ccwReading;
        if (ccwButtonState == LOW && isDoorOpen) { // Chỉ đóng nếu cửa đang mở
          motorRunning = true;
          Serial.println("Manual Close: Button CCW pressed");
          myStepper.step(-STEPS_FOR_2_5_REVOLUTIONS); // Quay ngược 2.3 vòng (phải qua trái)
          turnOffMotor(); // Tắt motor
          isDoorOpen = false;
          motorRunning = false;
        }
      }
    }
    lastCCWButtonState = ccwReading;

    // Xử lý RFID
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      // Đọc UID của thẻ
      Serial.print("UID tag :");
      String content = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
        content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
        content.concat(String(mfrc522.uid.uidByte[i], HEX));
      }
      Serial.println();
      Serial.print("Message : ");
      content.toUpperCase();

      // Kiểm tra UID hợp lệ
      if (content.substring(1) == "63 83 41 10" || 
          content.substring(1) == "7E 32 30 00" || 
          content.substring(1) == "FC F8 45 03" || 
          content.substring(1) == "95 79 1C 53" || 
          content.substring(1) == "F5 BC 0C 53" || 
          content.substring(1) == "F7 73 A1 D5") {
        Serial.println("Authorized access");
        if (!isDoorOpen) { // Chỉ mở nếu cửa chưa mở
          motorRunning = true;
          Serial.println("RFID Open: Authorized card");
          myStepper.step(STEPS_FOR_2_5_REVOLUTIONS); // Quay thuận 2.3 vòng (trái qua phải)
          turnOffMotor(); // Tắt motor
          isDoorOpen = true;
          motorRunning = false;
        } else {
          Serial.println("Door is already open");
        }
      } else {
        Serial.println("Access denied");
      }
      mfrc522.PICC_HaltA(); // Dừng đọc thẻ
    }
  }
}