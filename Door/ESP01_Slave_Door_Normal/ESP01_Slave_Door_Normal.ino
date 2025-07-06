#define DEVICE_ID 1
#define FIRMWARE_VERSION "4.0.0"

#include <ESP8266WiFi.h>
#include <espnow.h>

// GATEWAY MAC ADDRESS
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVVX08V40YMGTW";

// ESP-01 PIN CONFIGURATION
#define SERVO_PIN 2

// SERVO CONTROL
volatile int currentAngle = 0;
const int SERVO_MIN_PULSE = 544;
const int SERVO_MAX_PULSE = 2400;

// ✅ OPTIMIZED: Simplified ESP-NOW message structure - reduced size
struct CompactMessage {
  char type[4];        // "CMD", "ACK", "HBT" (3 chars + null)
  char action[4];      // "OPN", "CLS", "TGL" (3 chars + null)  
  int angle;           // Current servo angle
  bool success;        // Command success
  unsigned long ts;    // Timestamp (shortened)
};

// ✅ FIXED: Door state enum with prefixed names to avoid conflicts
enum DoorState { 
  DOOR_CLOSED = 0, 
  DOOR_OPENING = 1, 
  DOOR_OPEN = 2, 
  DOOR_CLOSING = 3 
};

DoorState doorState = DOOR_CLOSED;
bool isMoving = false;

// ✅ CRITICAL: Prevent callback-to-callback sending
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
  
  Serial.println("ESP-01 Door v4.0.0");
  Serial.println("ID:" + String(DEVICE_ID));
  Serial.println("Serial:" + DEVICE_SERIAL);
  
  // Initialize servo
  pinMode(SERVO_PIN, OUTPUT);
  setServoAngle(0);
  currentAngle = 0;
  doorState = DOOR_CLOSED;
  
  // Setup ESP-NOW
  setupESPNow();
  
  Serial.println("Ready");
  delay(1000);
  needSendHeartbeat = true;
}

void setupESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  
  wifi_set_channel(1);
  wifi_set_sleep_type(NONE_SLEEP_T);
  
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init FAIL");
    delay(3000);
    ESP.restart();
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  if (esp_now_add_peer(gatewayMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0) == 0) {
    Serial.println("Peer OK");
  } else {
    Serial.println("Peer FAIL");
  }
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  msgSent++;
  Serial.println("TX#" + String(msgSent) + (sendStatus == 0 ? " OK" : " FAIL"));
}

// ✅ OPTIMIZED: Minimal processing in callback
void onDataReceived(uint8_t *mac_addr, uint8_t *data, uint8_t len) {
  msgReceived++;
  
  if (memcmp(mac_addr, gatewayMAC, 6) != 0 || len != sizeof(CompactMessage)) {
    Serial.println("RX#" + String(msgReceived) + " BAD");
    return;
  }
  
  memcpy(&rxBuffer, data, len);
  
  gatewayOnline = true;
  lastGatewayMsg = millis();
  
  String type = String(rxBuffer.type);
  Serial.println("RX#" + String(msgReceived) + ":" + type);
  
  // ✅ CRITICAL: Set flags only - NO SENDING
  if (type == "CMD") {
    strncpy(pendingAction, rxBuffer.action, 3);
    pendingAction[3] = '\0';
    needSendResponse = true;
    
  } else if (type == "HBT") {
    needSendHeartbeat = true;
  }
}

// ✅ OPTIMIZED: Main loop processing with compact commands
void processCommand() {
  if (!needSendResponse) return;
  needSendResponse = false;
  
  String action = String(pendingAction);
  Serial.println("CMD:" + action);
  
  bool success = false;
  
  if (isMoving) {
    success = false;
    
  } else if (action == "OPN" && doorState == DOOR_CLOSED) {
    openDoor();
    success = true;
    
  } else if (action == "CLS" && doorState == DOOR_OPEN) {
    closeDoor();
    success = true;
    
  } else if (action == "TGL") {
    if (doorState == DOOR_CLOSED) {
      openDoor();
      success = true;
    } else if (doorState == DOOR_OPEN) {
      closeDoor();
      success = true;
    }
  }
  
  pendingSuccess = success;
  sendResponse();
}

void openDoor() {
  if (isMoving || doorState == DOOR_OPEN) return;
  
  Serial.println("Opening...");
  doorState = DOOR_OPENING;
  isMoving = true;
  
  moveServo(180);
  
  doorState = DOOR_OPEN;
  isMoving = false;
  Serial.println("Opened");
  needSendStatus = true;
}

void closeDoor() {
  if (isMoving || doorState == DOOR_CLOSED) return;
  
  Serial.println("Closing...");
  doorState = DOOR_CLOSING;
  isMoving = true;
  
  moveServo(0);
  
  doorState = DOOR_CLOSED;
  isMoving = false;
  Serial.println("Closed");
  needSendStatus = true;
}

void moveServo(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  
  int step = (angle > currentAngle) ? 5 : -5;
  
  while (currentAngle != angle) {
    if (abs(currentAngle - angle) < 5) {
      currentAngle = angle;
    } else {
      currentAngle += step;
    }
    
    setServoAngle(currentAngle);
    delay(20);
    yield();
  }
}

void setServoAngle(int angle) {
  int pulseWidth = map(angle, 0, 180, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(SERVO_PIN, LOW);
  delayMicroseconds(20000 - pulseWidth);
}

// ✅ OPTIMIZED: Compact response format
void sendResponse() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "ACK", 3);
  strncpy(txBuffer.action, pendingAction, 3);
  txBuffer.angle = currentAngle;
  txBuffer.success = pendingSuccess;
  txBuffer.ts = millis();
  
  int result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
  Serial.println("ACK:" + String(pendingAction) + "=" + String(pendingSuccess) + "(" + String(result) + ")");
}

void sendHeartbeat() {
  if (!needSendHeartbeat) return;
  needSendHeartbeat = false;
  
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "HBT", 3);
  strncpy(txBuffer.action, "ALV", 3);  // "ALIVE" shortened
  txBuffer.angle = currentAngle;
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  int result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
  Serial.println("HBT:" + String(result == 0 ? "OK" : "FAIL"));
  
  lastHeartbeat = millis();
}

void sendStatus() {
  if (!needSendStatus) return;
  needSendStatus = false;
  
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "STS", 3);  // "STATUS" shortened
  
  // ✅ OPTIMIZED: State as 3-char codes
  if (doorState == DOOR_CLOSED) strncpy(txBuffer.action, "CLD", 3);       // "CLOSED"
  else if (doorState == DOOR_OPENING) strncpy(txBuffer.action, "OPG", 3); // "OPENING"
  else if (doorState == DOOR_OPEN) strncpy(txBuffer.action, "OPD", 3);    // "OPENED"
  else strncpy(txBuffer.action, "CLG", 3);                                // "CLOSING"
  
  txBuffer.angle = currentAngle;
  txBuffer.success = true;
  txBuffer.ts = millis();
  
  int result = esp_now_send(gatewayMAC, (uint8_t*)&txBuffer, sizeof(txBuffer));
  Serial.println("STS:" + String(txBuffer.action) + "(" + String(result) + ")");
}

void checkConnection() {
  if (gatewayOnline && (millis() - lastGatewayMsg > 120000)) {
    gatewayOnline = false;
    Serial.println("GW Timeout");
  }
}

void loop() {
  // ✅ CRITICAL: All sending in main loop
  processCommand();
  sendHeartbeat();
  sendStatus();
  
  // Timers
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
                   " Door:" + String(doorState) + 
                   " Angle:" + String(currentAngle) + 
                   " TX/RX:" + String(msgSent) + "/" + String(msgReceived));
    lastPrint = millis();
  }
  
  // Keep servo stable
  if (!isMoving) {
    setServoAngle(currentAngle);
  }
  
  yield();
  delay(10);
}