#define DOOR_ID 10
#define FIRMWARE_VERSION "4.0.1"

#include <WiFi.h>
#include <esp_now.h>
#include <Stepper.h>
#include <EEPROM.h>

#define EEPROM_SIZE 64
#define ADDR_DOOR_STATE 0
#define ADDR_CLOSED_ROUNDS 4
#define ADDR_OPEN_ROUNDS 8
#define ADDR_PIR_ENABLED 12
#define ADDR_INIT_FLAG 16

uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVSQGM7E9S9D9A";  // Match Master config

#define PIR_PIN 32
#define BUTTON_PIN 33
#define MOTOR_PINS 16, 14, 15, 13

const int STEPS_PER_REVOLUTION = 2048;
const int DEFAULT_OPEN_ROUNDS = 1;
const int DEFAULT_CLOSED_ROUNDS = 0;

int openRounds = DEFAULT_OPEN_ROUNDS;
int closedRounds = DEFAULT_CLOSED_ROUNDS;
int currentRounds = 0;
bool pirEnabled = true;

Stepper myStepper(STEPS_PER_REVOLUTION, MOTOR_PINS);

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

// PIR motion detection with filtering
bool pirState = false;
bool lastPirState = false;
unsigned long lastPirTrigger = 0;
unsigned long doorOpenTime = 0;
unsigned long motionStoppedTime = 0;
const unsigned long PIR_DEBOUNCE = 2000; // 2s debounce
const unsigned long DOOR_OPEN_DELAY = 5000; // 5s auto-close after motion stops
const unsigned long MOTION_IGNORE_TIME = 10000; // Ignore PIR for 10s after door movement

// Button control
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonDebounce = 0;
const unsigned long buttonDebounceDelay = 50;

// ESP-NOW communication
volatile bool needSendResponse = false;
volatile bool needSendStatus = false;
char pendingAction[4] = "";
bool pendingSuccess = false;
bool gatewayOnline = false;
unsigned long lastGatewayMsg = 0;
unsigned long lastHeartbeat = 0;
static CompactMessage rxBuffer;
static CompactMessage txBuffer;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("Sliding Door v4.0.1");
  Serial.println("DOOR_ID:" + String(DOOR_ID));
  Serial.println("SN:" + DEVICE_SERIAL);
  
  pinMode(PIR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);
  
  myStepper.setSpeed(10);
  
  EEPROM.begin(EEPROM_SIZE);
  loadDoorState();
  
  doorState = isDoorOpen ? DOOR_OPEN : DOOR_CLOSED;
  currentRounds = isDoorOpen ? openRounds : closedRounds;
  
  setupESPNow();
  
  Serial.println("READY PIR:" + String(pirEnabled ? "ON" : "OFF"));
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
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.println("TX " + String(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL"));
}

void onDataReceived(const esp_now_recv_info *recv_info, const uint8_t *data, int len) {
  if (memcmp(recv_info->src_addr, gatewayMAC, 6) != 0 || len != sizeof(CompactMessage)) {
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
  }
}

void turnOffMotor() {
  digitalWrite(13, LOW);
  digitalWrite(14, LOW);
  digitalWrite(15, LOW);
  digitalWrite(16, LOW);
}

void operateSlidingDoor(bool openDoor) {
  if (isMoving) return;
  
  unsigned long currentTime = millis();
  
  if (openDoor && !isDoorOpen) {
    isMoving = true;
    doorState = DOOR_OPENING;
    Serial.println("OPENING:" + String(openRounds) + " rounds");
    
    int steps = openRounds * STEPS_PER_REVOLUTION;
    myStepper.step(steps);
    turnOffMotor();
    
    isDoorOpen = true;
    doorState = DOOR_OPEN;
    currentRounds = openRounds;
    doorOpenTime = currentTime;
    motionStoppedTime = currentTime + MOTION_IGNORE_TIME; // Ignore PIR initially
    isMoving = false;
    needSendStatus = true;
    
  } else if (!openDoor && isDoorOpen) {
    isMoving = true;
    doorState = DOOR_CLOSING;
    Serial.println("CLOSING:" + String(openRounds) + " rounds");
    
    int steps = openRounds * STEPS_PER_REVOLUTION;
    myStepper.step(-steps);
    turnOffMotor();
    
    isDoorOpen = false;
    doorState = DOOR_CLOSED;
    currentRounds = closedRounds;
    motionStoppedTime = currentTime + MOTION_IGNORE_TIME; // Ignore PIR after closing
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
      operateSlidingDoor(true);
      pendingSuccess = true;
    } else {
      pendingSuccess = false;
    }
    
  } else if (action == "CLS") {
    if (isDoorOpen && !isMoving) {
      operateSlidingDoor(false);
      pendingSuccess = true;
    } else {
      pendingSuccess = false;
    }
    
  } else if (action == "TGL") {
    if (!isMoving) {
      operateSlidingDoor(!isDoorOpen);
      pendingSuccess = true;
    } else {
      pendingSuccess = false;
    }
    
  } else if (action == "CFG") {
    sendConfigMessage();
    pendingSuccess = true;
    
  } else if (action == "PIR") {
    // Toggle PIR enable/disable
    pirEnabled = !pirEnabled;
    saveDoorState();
    Serial.println("PIR:" + String(pirEnabled ? "ON" : "OFF"));
    pendingSuccess = true;
    
  } else {
    pendingSuccess = false;
  }
  
  if (action != "CFG" && action != "PIR") {
    saveDoorState();
  }
  sendResponse();
}

void sendResponse() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  strncpy(txBuffer.type, "ACK", 3);
  strncpy(txBuffer.action, pendingAction, 3);
  txBuffer.angle = currentRounds * 90;
  txBuffer.success = pendingSuccess;
  txBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
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
  
  esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
}

void sendConfigMessage() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  strncpy(txBuffer.type, "CFG", 3);
  strncpy(txBuffer.action, "SLD", 3); // "SLIDING"
  txBuffer.angle = (pirEnabled ? 0x8000 : 0) | (closedRounds << 8) | openRounds;
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
}

void sendDeviceOnline() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  strncpy(txBuffer.type, "ONL", 3);
  strncpy(txBuffer.action, "CAP", 3);
  txBuffer.angle = DOOR_ID;  // Use DOOR_ID
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
}

void sendEnhancedCapabilities() {
  sendDeviceOnline();
  delay(500);
  sendConfigMessage();
  delay(500);
  needSendStatus = true;
}

void saveDoorState() {
  EEPROM.put(ADDR_DOOR_STATE, isDoorOpen);
  EEPROM.put(ADDR_CLOSED_ROUNDS, closedRounds);
  EEPROM.put(ADDR_OPEN_ROUNDS, openRounds);
  EEPROM.put(ADDR_PIR_ENABLED, pirEnabled);
  EEPROM.put(ADDR_INIT_FLAG, 0xCC);
  EEPROM.commit();
}

void loadDoorState() {
  uint8_t initFlag;
  EEPROM.get(ADDR_INIT_FLAG, initFlag);
  
  if (initFlag == 0xCC) {
    EEPROM.get(ADDR_DOOR_STATE, isDoorOpen);
    EEPROM.get(ADDR_CLOSED_ROUNDS, closedRounds);
    EEPROM.get(ADDR_OPEN_ROUNDS, openRounds);
    EEPROM.get(ADDR_PIR_ENABLED, pirEnabled);
    
    if (closedRounds < 0 || closedRounds > 10) closedRounds = DEFAULT_CLOSED_ROUNDS;
    if (openRounds < 1 || openRounds > 10) openRounds = DEFAULT_OPEN_ROUNDS;
    
    currentRounds = isDoorOpen ? openRounds : closedRounds;
    Serial.println("STATE LOADED");
  } else {
    isDoorOpen = false;
    closedRounds = DEFAULT_CLOSED_ROUNDS;
    openRounds = DEFAULT_OPEN_ROUNDS;
    pirEnabled = true;
    currentRounds = closedRounds;
    Serial.println("DEFAULT STATE");
    saveDoorState();
  }
}

void handlePIRMotion() {
  if (!pirEnabled || isMoving) return;
  
  unsigned long currentTime = millis();
  
  // Ignore PIR during motion ignore period
  if (currentTime < motionStoppedTime) return;
  
  bool currentPirState = digitalRead(PIR_PIN);
  
  // PIR state change with debounce
  if (currentPirState != lastPirState) {
    if (currentTime - lastPirTrigger > PIR_DEBOUNCE) {
      pirState = currentPirState;
      lastPirTrigger = currentTime;
      
      if (pirState) {
        // Motion detected
        if (!isDoorOpen) {
          Serial.println("PIR OPEN");
          operateSlidingDoor(true);
        } else {
          // Reset auto-close timer
          doorOpenTime = currentTime;
        }
      } else {
        // Motion stopped
        if (isDoorOpen) {
          motionStoppedTime = currentTime;
          Serial.println("PIR STOP");
        }
      }
    }
  }
  lastPirState = currentPirState;
  
  // Auto-close after motion stops
  if (isDoorOpen && !pirState && 
      (currentTime - motionStoppedTime > DOOR_OPEN_DELAY) &&
      (currentTime > motionStoppedTime)) {
    Serial.println("AUTO CLOSE");
    operateSlidingDoor(false);
  }
}

void handleManualButton() {
  if (isMoving) return;
  
  bool reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastButtonDebounce = millis();
  }
  
  if ((millis() - lastButtonDebounce) > buttonDebounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        Serial.println("BTN TOGGLE");
        operateSlidingDoor(!isDoorOpen);
      }
    }
  }
  lastButtonState = reading;
}

void loop() {
  processCommand();
  sendStatus();
  
  if (!isMoving) {
    handlePIRMotion();
    handleManualButton();
  }
  
  static unsigned long lastHeartbeatCheck = 0;
  if (millis() - lastHeartbeatCheck > 30000) {
    // Send alive message
    memset(&txBuffer, 0, sizeof(txBuffer));
    strncpy(txBuffer.type, "HBT", 3);
    strncpy(txBuffer.action, "ALV", 3);
    txBuffer.angle = currentRounds * 90;
    txBuffer.success = true;
    txBuffer.ts = millis();
    esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
    lastHeartbeatCheck = millis();
  }
  
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 45000) {
    Serial.println("Door:" + String(isDoorOpen ? "OPEN" : "CLOSED") + 
                   " PIR:" + String(pirEnabled ? "ON" : "OFF") + 
                   " Rounds:" + String(currentRounds));
    lastPrint = millis();
  }
  
  yield();
  delay(10);
}