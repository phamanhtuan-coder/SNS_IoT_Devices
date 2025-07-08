#define DEVICE_ID 9  // Rolling Door ID = 9
#define FIRMWARE_VERSION "4.0.0"

#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Stepper.h>
#include <Ticker.h>

// GATEWAY MAC ADDRESS
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVZ9XM4N8Q7GWP";  // Rolling door serial

// RFID Configuration
#define SS_PIN 21  // SDA for ESP32
#define RST_PIN 22 // RST for ESP32

// Stepper Motor Configuration (28BYJ-48)
const int STEPS_PER_REVOLUTION = 2048;
const int STEPS_FOR_2_5_REVOLUTIONS = STEPS_PER_REVOLUTION * 2.3; // 2.3 revolutions

// Button pins
const int buttonCCWPin = 25;  // Close door (CCW, right to left)
const int buttonCWPin = 26;   // Open door (CW, left to right)

// Initialize stepper motor (IN1, IN3, IN2, IN4)
Stepper myStepper(STEPS_PER_REVOLUTION, 16, 14, 15, 13);

// Initialize RFID
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ESP-NOW message structure (matching other slaves)
struct CompactMessage {
  char type[4];        // "CMD", "ACK", "HBT" (3 chars + null)
  char action[4];      // "OPN", "CLS", "TGL" (3 chars + null)  
  int angle;           // Current servo angle (0=closed, 180=open)
  bool success;        // Command success
  unsigned long ts;    // Timestamp
};

// Door state enum
enum DoorState { 
  DOOR_CLOSED = 0, 
  DOOR_OPENING = 1, 
  DOOR_OPEN = 2, 
  DOOR_CLOSING = 3 
};

DoorState doorState = DOOR_CLOSED;
bool isMoving = false;
bool isDoorOpen = false; // Door status: false = closed, true = open

// Button debouncing
int cwButtonState = HIGH;
int ccwButtonState = HIGH;
int lastCWButtonState = HIGH;
int lastCCWButtonState = HIGH;
unsigned long lastCWDebounceTime = 0;
unsigned long lastCCWDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Prevent callback-to-callback sending
volatile bool needSendHeartbeat = false;
volatile bool needSendResponse = false;
volatile bool needSendStatus = false;

// Response data for main loop
char pendingAction[4] = "";
bool pendingSuccess = false;

// CONNECTION STATUS
bool gatewayOnline = false;
unsigned long lastGatewayMsg = 0;
unsigned long lastHeartbeat = 0;

// MEMORY PROTECTION
static CompactMessage rxBuffer;
static CompactMessage txBuffer;

// COUNTERS
unsigned long msgSent = 0;
unsigned long msgReceived = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("ESP32 Rolling Door v4.0.0");
  Serial.println("ID:" + String(DEVICE_ID));
  Serial.println("Serial:" + DEVICE_SERIAL);
  Serial.println("MAC:" + WiFi.macAddress());
  
  // Initialize SPI and RFID
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("RFID initialized - Approximate your card to the reader...");
  
  // Setup stepper motor
  myStepper.setSpeed(10); // 10 RPM
  
  // Setup button pins
  pinMode(buttonCWPin, INPUT_PULLUP);   // GPIO 26: Open door (CW)
  pinMode(buttonCCWPin, INPUT_PULLUP);  // GPIO 25: Close door (CCW)
  
  // Setup motor pins
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);
  
  // Read initial button states
  cwButtonState = digitalRead(buttonCWPin);
  ccwButtonState = digitalRead(buttonCCWPin);
  lastCWButtonState = cwButtonState;
  lastCCWButtonState = ccwButtonState;
  
  // Ensure door is in closed state initially
  doorState = DOOR_CLOSED;
  isDoorOpen = false;
  
  // Setup ESP-NOW
  setupESPNow();
  
  Serial.println("Rolling Door Ready");
  delay(1000);
  needSendHeartbeat = true;
}

void setupESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init FAIL");
    delay(3000);
    ESP.restart();
  }
  
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  // Add gateway peer
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("Gateway Peer OK");
  } else {
    Serial.println("Gateway Peer FAIL");
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  msgSent++;
  Serial.println("TX#" + String(msgSent) + (status == ESP_NOW_SEND_SUCCESS ? " OK" : " FAIL"));
}

void onDataReceived(const esp_now_recv_info *recv_info, const uint8_t *data, int len) {
  msgReceived++;
  
  if (memcmp(recv_info->src_addr, gatewayMAC, 6) != 0 || len != sizeof(CompactMessage)) {
    Serial.println("RX#" + String(msgReceived) + " BAD");
    return;
  }
  
  memcpy(&rxBuffer, data, len);
  
  gatewayOnline = true;
  lastGatewayMsg = millis();
  
  String type = String(rxBuffer.type);
  Serial.println("RX#" + String(msgReceived) + ":" + type);
  
  if (type == "CMD") {
    strncpy(pendingAction, rxBuffer.action, 3);
    pendingAction[3] = '\0';
    needSendResponse = true;
    
  } else if (type == "HBT") {
    needSendHeartbeat = true;
  }
}

void turnOffMotor() {
  // Turn off all motor control pins
  digitalWrite(13, LOW);
  digitalWrite(14, LOW);
  digitalWrite(15, LOW);
  digitalWrite(16, LOW);
}

void operateRollingDoor(bool openDoor) {
  if (isMoving) return;
  
  if (openDoor && !isDoorOpen) {
    // Open door (CW, left to right)
    isMoving = true;
    doorState = DOOR_OPENING;
    Serial.println("Opening rolling door - CW rotation");
    myStepper.step(STEPS_FOR_2_5_REVOLUTIONS); // 2.3 revolutions clockwise
    turnOffMotor();
    isDoorOpen = true;
    doorState = DOOR_OPEN;
    isMoving = false;
    needSendStatus = true;
    
  } else if (!openDoor && isDoorOpen) {
    // Close door (CCW, right to left)
    isMoving = true;
    doorState = DOOR_CLOSING;
    Serial.println("Closing rolling door - CCW rotation");
    myStepper.step(-STEPS_FOR_2_5_REVOLUTIONS); // 2.3 revolutions counter-clockwise
    turnOffMotor();
    isDoorOpen = false;
    doorState = DOOR_CLOSED;
    isMoving = false;
    needSendStatus = true;
  }
}

void processCommand() {
  if (!needSendResponse) return;
  needSendResponse = false;
  
  String action = String(pendingAction);
  
  if (action == "OPN") {
    if (!isDoorOpen && !isMoving) {
      operateRollingDoor(true);
      pendingSuccess = true;
      Serial.println("ESP-NOW Command: Open door");
    } else {
      pendingSuccess = false;
      Serial.println("Cannot open - door already open or moving");
    }
    
  } else if (action == "CLS") {
    if (isDoorOpen && !isMoving) {
      operateRollingDoor(false);
      pendingSuccess = true;
      Serial.println("ESP-NOW Command: Close door");
    } else {
      pendingSuccess = false;
      Serial.println("Cannot close - door already closed or moving");
    }
    
  } else if (action == "TGL") {
    if (!isMoving) {
      operateRollingDoor(!isDoorOpen);
      pendingSuccess = true;
      Serial.println("ESP-NOW Command: Toggle door");
    } else {
      pendingSuccess = false;
      Serial.println("Cannot toggle - door is moving");
    }
  } else {
    pendingSuccess = false;
    Serial.println("Unknown command: " + action);
  }
  
  sendResponse();
}

void sendResponse() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "ACK", 3);
  strncpy(txBuffer.action, pendingAction, 3);
  txBuffer.angle = isDoorOpen ? 180 : 0;  // 0 = closed, 180 = open
  txBuffer.success = pendingSuccess;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
  Serial.println("ACK:" + String(pendingAction) + "=" + String(pendingSuccess) + 
                 "(" + String(result == ESP_OK ? "OK" : "FAIL") + ")");
}

void sendHeartbeat() {
  if (!needSendHeartbeat) return;
  needSendHeartbeat = false;
  
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "HBT", 3);
  strncpy(txBuffer.action, "ALV", 3);
  txBuffer.angle = isDoorOpen ? 180 : 0;
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
  Serial.println("HBT:" + String(result == ESP_OK ? "OK" : "FAIL"));
  
  lastHeartbeat = millis();
}

void sendStatus() {
  if (!needSendStatus) return;
  needSendStatus = false;
  
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "STS", 3);
  
  // State as 3-char codes
  if (doorState == DOOR_CLOSED) strncpy(txBuffer.action, "CLD", 3);
  else if (doorState == DOOR_OPENING) strncpy(txBuffer.action, "OPG", 3);
  else if (doorState == DOOR_OPEN) strncpy(txBuffer.action, "OPD", 3);
  else strncpy(txBuffer.action, "CLG", 3);
  
  txBuffer.angle = isDoorOpen ? 180 : 0;
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
  Serial.println("STS:" + String(txBuffer.action) + "(" + String(result == ESP_OK ? "OK" : "FAIL") + ")");
}

void handleManualButtons() {
  if (isMoving) return; // Don't process buttons while motor is running
  
  int cwReading = digitalRead(buttonCWPin);    // GPIO 26: Open door (CW)
  int ccwReading = digitalRead(buttonCCWPin);  // GPIO 25: Close door (CCW)

  // Handle open door button (CW, left to right)
  if (cwReading != lastCWButtonState) {
    lastCWDebounceTime = millis();
  }
  if ((millis() - lastCWDebounceTime) > debounceDelay) {
    if (cwReading != cwButtonState) {
      cwButtonState = cwReading;
      if (cwButtonState == LOW && !isDoorOpen) { // Only open if door is closed
        Serial.println("Manual Open: Button CW pressed");
        operateRollingDoor(true);
      }
    }
  }
  lastCWButtonState = cwReading;

  // Handle close door button (CCW, right to left)
  if (ccwReading != lastCCWButtonState) {
    lastCCWDebounceTime = millis();
  }
  if ((millis() - lastCCWDebounceTime) > debounceDelay) {
    if (ccwReading != ccwButtonState) {
      ccwButtonState = ccwReading;
      if (ccwButtonState == LOW && isDoorOpen) { // Only close if door is open
        Serial.println("Manual Close: Button CCW pressed");
        operateRollingDoor(false);
      }
    }
  }
  lastCCWButtonState = ccwReading;
}

void handleRFID() {
  if (isMoving) return; // Don't process RFID while motor is running
  
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    // Read card UID
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

    // Check valid UIDs
    if (content.substring(1) == "63 83 41 10" || 
        content.substring(1) == "7E 32 30 00" || 
        content.substring(1) == "FC F8 45 03" || 
        content.substring(1) == "95 79 1C 53" || 
        content.substring(1) == "F5 BC 0C 53" || 
        content.substring(1) == "F7 73 A1 D5") {
      Serial.println("Authorized access");
      if (!isDoorOpen) { // Only open if door is closed
        Serial.println("RFID Open: Authorized card");
        operateRollingDoor(true);
      } else {
        Serial.println("Door is already open");
      }
    } else {
      Serial.println("Access denied");
    }
    mfrc522.PICC_HaltA(); // Stop reading card
  }
}

void checkConnection() {
  if (gatewayOnline && (millis() - lastGatewayMsg > 120000)) {
    gatewayOnline = false;
    Serial.println("GW Timeout");
  }
}

void loop() {
  // Process ESP-NOW commands and send responses
  processCommand();
  sendHeartbeat();
  sendStatus();
  
  // Handle manual controls only when not processing ESP-NOW commands
  if (!needSendResponse && !isMoving) {
    handleManualButtons();
    handleRFID();
  }
  
  // Heartbeat timer
  if (millis() - lastHeartbeat > 30000) {
    needSendHeartbeat = true;
  }
  
  // Connection check
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    checkConnection();
    lastCheck = millis();
  }
  
  // Status print
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 45000) {
    Serial.println("GW:" + String(gatewayOnline ? "ON" : "OFF") + 
                   " Door:" + String(isDoorOpen ? "OPEN" : "CLOSED") + 
                   " State:" + String(doorState) +
                   " TX/RX:" + String(msgSent) + "/" + String(msgReceived));
    lastPrint = millis();
  }
  
  yield();
  delay(10);
}