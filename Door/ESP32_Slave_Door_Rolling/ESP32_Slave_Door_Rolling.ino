#define DOOR_ID 9  // Use DOOR_ID to match Master gateway
#define FIRMWARE_VERSION "4.0.1"

#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Stepper.h>
#include <Ticker.h>
#include <EEPROM.h>

#define EEPROM_SIZE 64
#define ADDR_DOOR_STATE 0
#define ADDR_CLOSED_ROUNDS 4
#define ADDR_OPEN_ROUNDS 8
#define ADDR_INIT_FLAG 12

uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVVSBGRTM0TRFW";  // Match Master config

#define SS_PIN 21
#define RST_PIN 22

const int STEPS_PER_REVOLUTION = 2048;
const int DEFAULT_OPEN_ROUNDS = 2;
const int DEFAULT_CLOSED_ROUNDS = 0;

int openRounds = DEFAULT_OPEN_ROUNDS;
int closedRounds = DEFAULT_CLOSED_ROUNDS;
int currentRounds = 0;

const int buttonCCWPin = 25;
const int buttonCWPin = 26;

Stepper myStepper(STEPS_PER_REVOLUTION, 16, 14, 15, 13);
MFRC522 mfrc522(SS_PIN, RST_PIN);

struct CompactMessage {
  char type[4];
  char action[4];
  int angle;
  bool success;
  unsigned long ts;
};

enum DoorState { 
  DOOR_CLOSED = 0, 
  DOOR_OPENING = 1, 
  DOOR_OPEN = 2, 
  DOOR_CLOSING = 3 
};

DoorState doorState = DOOR_CLOSED;
bool isMoving = false;
bool isDoorOpen = false;

int cwButtonState = HIGH;
int ccwButtonState = HIGH;
int lastCWButtonState = HIGH;
int lastCCWButtonState = HIGH;
unsigned long lastCWDebounceTime = 0;
unsigned long lastCCWDebounceTime = 0;
const unsigned long debounceDelay = 50;

volatile bool needSendHeartbeat = false;
volatile bool needSendResponse = false;
volatile bool needSendStatus = false;

char pendingAction[4] = "";
bool pendingSuccess = false;

bool gatewayOnline = false;
unsigned long lastGatewayMsg = 0;
unsigned long lastHeartbeat = 0;

static CompactMessage rxBuffer;
static CompactMessage txBuffer;

unsigned long msgSent = 0;
unsigned long msgReceived = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("ESP32 Rolling Door v4.0.1");
  Serial.println("DOOR_ID:" + String(DOOR_ID));
  Serial.println("SN:" + DEVICE_SERIAL);
  Serial.println("MAC:" + WiFi.macAddress());
  
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("RFID OK");
  
  myStepper.setSpeed(10);
  
  pinMode(buttonCWPin, INPUT_PULLUP);
  pinMode(buttonCCWPin, INPUT_PULLUP);
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);
  
  cwButtonState = digitalRead(buttonCWPin);
  ccwButtonState = digitalRead(buttonCCWPin);
  lastCWButtonState = cwButtonState;
  lastCCWButtonState = ccwButtonState;
  
  EEPROM.begin(EEPROM_SIZE);
  loadDoorState();
  
  doorState = isDoorOpen ? DOOR_OPEN : DOOR_CLOSED;
  currentRounds = isDoorOpen ? openRounds : closedRounds;
  
  setupESPNow();
  
  Serial.println("READY");
  delay(1000);
  sendEnhancedCapabilities();
}

void setupESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESPNOW FAIL");
    delay(3000);
    ESP.restart();
  }
  
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("PEER OK");
  } else {
    Serial.println("PEER FAIL");
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  msgSent++;
  Serial.println("TX#" + String(msgSent) + (status == ESP_NOW_SEND_SUCCESS ? " OK" : " FAIL"));
}

void onDataReceived(const esp_now_recv_info *recv_info, const uint8_t *data, int len) {
  msgReceived++;
  
  if (memcmp(recv_info->src_addr, gatewayMAC, 6) != 0 || len != sizeof(CompactMessage)) {
    Serial.println("RX BAD");
    return;
  }
  
  memcpy(&rxBuffer, data, len);
  
  gatewayOnline = true;
  lastGatewayMsg = millis();
  
  String type = String(rxBuffer.type);
  String action = String(rxBuffer.action);
  Serial.println("RX:" + type + ":" + action);
  
  if (type == "CMD") {
    strncpy(pendingAction, rxBuffer.action, 3);
    pendingAction[3] = '\0';
    needSendResponse = true;
    
  } else if (type == "HBT") {
    needSendHeartbeat = true;
  }
}

void turnOffMotor() {
  digitalWrite(13, LOW);
  digitalWrite(14, LOW);
  digitalWrite(15, LOW);
  digitalWrite(16, LOW);
}

void operateRollingDoor(bool openDoor) {
  if (isMoving) return;
  
  if (openDoor && !isDoorOpen) {
    isMoving = true;
    doorState = DOOR_OPENING;
    Serial.println("OPENING CW:" + String(openRounds) + " rounds");
    
    int steps = openRounds * STEPS_PER_REVOLUTION;
    myStepper.step(steps);
    turnOffMotor();
    
    isDoorOpen = true;
    doorState = DOOR_OPEN;
    currentRounds = openRounds;
    isMoving = false;
    needSendStatus = true;
    
  } else if (!openDoor && isDoorOpen) {
    isMoving = true;
    doorState = DOOR_CLOSING;
    Serial.println("CLOSING CCW:" + String(openRounds) + " rounds");
    
    int steps = openRounds * STEPS_PER_REVOLUTION;
    myStepper.step(-steps);
    turnOffMotor();
    
    isDoorOpen = false;
    doorState = DOOR_CLOSED;
    currentRounds = closedRounds;
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
      Serial.println("CMD OPEN");
    } else {
      pendingSuccess = false;
    }
    
  } else if (action == "CLS") {
    if (isDoorOpen && !isMoving) {
      operateRollingDoor(false);
      pendingSuccess = true;
      Serial.println("CMD CLOSE");
    } else {
      pendingSuccess = false;
    }
    
  } else if (action == "TGL") {
    if (!isMoving) {
      operateRollingDoor(!isDoorOpen);
      pendingSuccess = true;
      Serial.println("CMD TOGGLE");
    } else {
      pendingSuccess = false;
    }
    
  } else if (action == "CFG") {
    sendConfigMessage();
    pendingSuccess = true;
    
  } else {
    pendingSuccess = false;
  }
  
  saveDoorState();
  sendResponse();
}

void sendResponse() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "ACK", 3);
  strncpy(txBuffer.action, pendingAction, 3);
  txBuffer.angle = currentRounds * 90; // Convert rounds to angle equivalent
  txBuffer.success = pendingSuccess;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
}

void sendHeartbeat() {
  if (!needSendHeartbeat) return;
  needSendHeartbeat = false;
  
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "HBT", 3);
  strncpy(txBuffer.action, "ALV", 3);
  txBuffer.angle = currentRounds * 90;
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
  lastHeartbeat = millis();
}

void sendStatus() {
  if (!needSendStatus) return;
  needSendStatus = false;
  
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "STS", 3);
  
  if (doorState == DOOR_CLOSED) strncpy(txBuffer.action, "CLD", 3);
  else if (doorState == DOOR_OPENING) strncpy(txBuffer.action, "OPG", 3);
  else if (doorState == DOOR_OPEN) strncpy(txBuffer.action, "OPD", 3);
  else strncpy(txBuffer.action, "CLG", 3);
  
  txBuffer.angle = currentRounds * 90;
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
}

void sendConfigMessage() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  strncpy(txBuffer.type, "CFG", 3);
  strncpy(txBuffer.action, "RND", 3); // "ROUNDS"
  txBuffer.angle = (closedRounds << 8) | openRounds; // Pack both rounds
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
}

void sendDeviceOnline() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  strncpy(txBuffer.type, "ONL", 3);
  strncpy(txBuffer.action, "CAP", 3);
  txBuffer.angle = DOOR_ID;  // Use DOOR_ID
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
}

void sendEnhancedCapabilities() {
  sendDeviceOnline();
  delay(500);
  sendConfigMessage();
  delay(500);
  needSendStatus = true;
  sendStatus();
}

void saveDoorState() {
  EEPROM.put(ADDR_DOOR_STATE, isDoorOpen);
  EEPROM.put(ADDR_CLOSED_ROUNDS, closedRounds);
  EEPROM.put(ADDR_OPEN_ROUNDS, openRounds);
  EEPROM.put(ADDR_INIT_FLAG, 0xBB);
  EEPROM.commit();
}

void loadDoorState() {
  uint8_t initFlag;
  EEPROM.get(ADDR_INIT_FLAG, initFlag);
  
  if (initFlag == 0xBB) {
    EEPROM.get(ADDR_DOOR_STATE, isDoorOpen);
    EEPROM.get(ADDR_CLOSED_ROUNDS, closedRounds);
    EEPROM.get(ADDR_OPEN_ROUNDS, openRounds);
    
    if (closedRounds < 0 || closedRounds > 10) closedRounds = DEFAULT_CLOSED_ROUNDS;
    if (openRounds < 1 || openRounds > 10) openRounds = DEFAULT_OPEN_ROUNDS;
    
    currentRounds = isDoorOpen ? openRounds : closedRounds;
    Serial.println("STATE LOADED");
  } else {
    isDoorOpen = false;
    closedRounds = DEFAULT_CLOSED_ROUNDS;
    openRounds = DEFAULT_OPEN_ROUNDS;
    currentRounds = closedRounds;
    Serial.println("DEFAULT STATE");
    saveDoorState();
  }
}

void handleManualButtons() {
  if (isMoving) return;
  
  int cwReading = digitalRead(buttonCWPin);
  int ccwReading = digitalRead(buttonCCWPin);

  if (cwReading != lastCWButtonState) {
    lastCWDebounceTime = millis();
  }
  if ((millis() - lastCWDebounceTime) > debounceDelay) {
    if (cwReading != cwButtonState) {
      cwButtonState = cwReading;
      if (cwButtonState == LOW && !isDoorOpen) {
        Serial.println("BTN OPEN");
        operateRollingDoor(true);
      }
    }
  }
  lastCWButtonState = cwReading;

  if (ccwReading != lastCCWButtonState) {
    lastCCWDebounceTime = millis();
  }
  if ((millis() - lastCCWDebounceTime) > debounceDelay) {
    if (ccwReading != ccwButtonState) {
      ccwButtonState = ccwReading;
      if (ccwButtonState == LOW && isDoorOpen) {
        Serial.println("BTN CLOSE");
        operateRollingDoor(false);
      }
    }
  }
  lastCCWButtonState = ccwReading;
}

void handleRFID() {
  if (isMoving) return;
  
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String content = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
      content.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    content.toUpperCase();

    if (content.substring(1) == "63 83 41 10" || 
        content.substring(1) == "7E 32 30 00" || 
        content.substring(1) == "FC F8 45 03" || 
        content.substring(1) == "95 79 1C 53" || 
        content.substring(1) == "F5 BC 0C 53" || 
        content.substring(1) == "F7 73 A1 D5") {
      Serial.println("RFID AUTH");
      if (!isDoorOpen) {
        operateRollingDoor(true);
      }
    } else {
      Serial.println("RFID DENY");
    }
    mfrc522.PICC_HaltA();
  }
}

void checkConnection() {
  if (gatewayOnline && (millis() - lastGatewayMsg > 120000)) {
    gatewayOnline = false;
    Serial.println("GW TIMEOUT");
  }
}

void loop() {
  processCommand();
  sendHeartbeat();
  sendStatus();
  
  if (!needSendResponse && !isMoving) {
    handleManualButtons();
    handleRFID();
  }
  
  if (millis() - lastHeartbeat > 30000) {
    needSendHeartbeat = true;
  }
  
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    checkConnection();
    lastCheck = millis();
  }
  
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 45000) {
    Serial.println("GW:" + String(gatewayOnline ? "ON" : "OFF") + 
                   " Door:" + String(isDoorOpen ? "OPEN" : "CLOSED") + 
                   " Rounds:" + String(currentRounds));
    lastPrint = millis();
  }
  
  yield();
  delay(10);
}