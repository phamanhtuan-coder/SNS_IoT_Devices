#define DEVICE_ID 8
#define FIRMWARE_VERSION "3.0.5"

#include <ESP8266WiFi.h>
#include <espnow.h>

// GATEWAY MAC ADDRESS
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVTH6PWR9ETXC2";

// ESP-01 PIN CONFIGURATION
#define SERVO_PIN 2

// SERVO CONTROL
volatile int currentAngle = 0;
const int SERVO_MIN_PULSE = 544;
const int SERVO_MAX_PULSE = 2400;

// ESP-NOW MESSAGE STRUCTURE
struct ESPNowMessage {
  char messageType[16];
  char serialNumber[32];
  char action[16];
  int servoAngle;
  bool success;
  char result[32];
  unsigned long timestamp;
};

// ✅ SAFE STRING COPY
void safeStringCopy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
  }
}

// DOOR STATE
enum DoorState { DOOR_CLOSED = 0, OPENING = 1, OPEN = 2, DOOR_CLOSING = 3 };
DoorState currentState = DOOR_CLOSED;
bool isMoving = false;

// ✅ CRITICAL: Prevent callback-to-callback sending
volatile bool needToSendHeartbeat = false;
volatile bool needToSendCommandResponse = false;
volatile bool needToSendStatus = false;

// Response data for main loop processing
String pendingCommandAction = "";
bool pendingCommandSuccess = false;
String pendingCommandResult = "";

// CONNECTION STATUS
bool gatewayOnline = false;
unsigned long lastGatewayMessage = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastStatusSent = 0;

// MEMORY PROTECTION - Smaller buffers for ESP-01
static ESPNowMessage receiveBuffer;
static ESPNowMessage sendBuffer;

// COUNTERS
unsigned long messagesSent = 0;
unsigned long messagesReceived = 0;

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  Serial.println("\n=== ESP-01 Door Controller STABLE v3.0.5 ===");
  Serial.println("Device: " + String(DEVICE_ID));
  Serial.println("Serial: " + DEVICE_SERIAL);
  Serial.println("My MAC: " + WiFi.macAddress());
  
  // Initialize servo
  pinMode(SERVO_PIN, OUTPUT);
  setServoAngle(0);
  currentAngle = 0;
  currentState = DOOR_CLOSED;
  Serial.println("[SERVO] ✓ Initialized at 0°");
  
  // Setup ESP-NOW
  setupESPNowStable();
  
  Serial.println("[INIT] ✓ ESP-01 Ready");
  
  // Initial heartbeat after delay
  delay(2000);
  needToSendHeartbeat = true;
  
  Serial.println("[INIT] ✓ Setup complete\n");
}

void setupESPNowStable() {
  Serial.println("[ESP-NOW] Stable setup...");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  wifi_set_channel(1);
  wifi_set_sleep_type(NONE_SLEEP_T);
  
  int initResult = esp_now_init();
  if (initResult != 0) {
    Serial.println("[ESP-NOW] Init failed: " + String(initResult));
    delay(5000);
    ESP.restart();
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSentStable);
  esp_now_register_recv_cb(onDataReceivedStable);
  
  int result = esp_now_add_peer(gatewayMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  if (result != 0) {
    result = esp_now_add_peer(gatewayMAC, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
  }
  
  if (result == 0) {
    Serial.println("[ESP-NOW] ✓ Peer added");
  } else {
    Serial.println("[ESP-NOW] ✗ Peer failed: " + String(result));
  }
}

void onDataSentStable(uint8_t *mac_addr, uint8_t sendStatus) {
  messagesSent++;
  if (sendStatus == 0) {
    Serial.println("[ESP-NOW] TX #" + String(messagesSent) + " ✓");
  } else {
    Serial.println("[ESP-NOW] TX #" + String(messagesSent) + " ✗ " + String(sendStatus));
  }
}

// ✅ CRITICAL FIX: Minimal processing in callback
void onDataReceivedStable(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  messagesReceived++;
  
  // Quick validation
  if (memcmp(mac_addr, gatewayMAC, 6) != 0 || len != sizeof(ESPNowMessage)) {
    Serial.println("[ESP-NOW] RX #" + String(messagesReceived) + " ✗ Invalid");
    return;
  }
  
  // Quick copy
  memcpy(&receiveBuffer, incomingData, len);
  
  // Update connection status
  gatewayOnline = true;
  lastGatewayMessage = millis();
  
  String msgType = String(receiveBuffer.messageType);
  Serial.println("[ESP-NOW] RX #" + String(messagesReceived) + ": " + msgType);
  
  // ✅ CRITICAL: Set flags for main loop processing - NO SENDING HERE
  if (msgType == "command") {
    pendingCommandAction = String(receiveBuffer.action);
    needToSendCommandResponse = true;
    
  } else if (msgType == "heartbeat") {
    needToSendHeartbeat = true;
  }
  
  // NO ESP-NOW SENDING FROM CALLBACK!
}

// ✅ SAFE: Main loop processing
void processCommand() {
  if (!needToSendCommandResponse) return;
  needToSendCommandResponse = false;
  
  String action = pendingCommandAction;
  Serial.println("[CMD] Processing: " + action);
  
  bool success = false;
  String result = "";
  
  if (isMoving) {
    result = "busy";
    success = false;
    
  } else if (action == "open_door" && currentState == DOOR_CLOSED) {
    openDoor();
    success = true;
    result = "opening";
    
  } else if (action == "close_door" && currentState == OPEN) {
    closeDoor();
    success = true;
    result = "closing";
    
  } else if (action == "toggle_door") {
    if (currentState == DOOR_CLOSED) {
      openDoor();
      result = "toggle_open";
    } else if (currentState == OPEN) {
      closeDoor();
      result = "toggle_close";
    } else {
      result = "moving";
    }
    success = true;
    
  } else {
    result = "invalid";
    success = false;
  }
  
  // Store for sending
  pendingCommandAction = action;
  pendingCommandSuccess = success;
  pendingCommandResult = result;
  
  // Send response
  sendCommandResponse();
}

void openDoor() {
  if (isMoving) return;
  Serial.println("[DOOR] Opening...");
  currentState = OPENING;
  isMoving = true;
  
  moveServo(180);
  
  currentState = OPEN;
  isMoving = false;
  Serial.println("[DOOR] ✓ Opened");
  needToSendStatus = true;
}

void closeDoor() {
  if (isMoving) return;
  Serial.println("[DOOR] Closing...");
  currentState = DOOR_CLOSING;
  isMoving = true;
  
  moveServo(0);
  
  currentState = DOOR_CLOSED;
  isMoving = false;
  Serial.println("[DOOR] ✓ Closed");
  needToSendStatus = true;
}

void moveServo(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  
  Serial.println("[SERVO] " + String(currentAngle) + "° → " + String(angle) + "°");
  
  int step = (angle > currentAngle) ? 3 : -3;
  
  while (currentAngle != angle) {
    if (abs(currentAngle - angle) < 3) {
      currentAngle = angle;
    } else {
      currentAngle += step;
    }
    
    setServoAngle(currentAngle);
    delay(15);
    yield();
  }
  
  Serial.println("[SERVO] ✓ At " + String(currentAngle) + "°");
}

void setServoAngle(int angle) {
  int pulseWidth = map(angle, 0, 180, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(SERVO_PIN, LOW);
  delayMicroseconds(20000 - pulseWidth);
}

// ✅ SAFE: Send functions called from main loop only
void sendCommandResponse() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "cmd_resp", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.serialNumber, DEVICE_SERIAL.c_str(), sizeof(sendBuffer.serialNumber));
  safeStringCopy(sendBuffer.action, pendingCommandAction.c_str(), sizeof(sendBuffer.action));
  safeStringCopy(sendBuffer.result, pendingCommandResult.c_str(), sizeof(sendBuffer.result));
  
  sendBuffer.servoAngle = currentAngle;
  sendBuffer.success = pendingCommandSuccess;
  sendBuffer.timestamp = millis();
  
  int result = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  Serial.println("[RESP] " + pendingCommandAction + " = " + String(pendingCommandSuccess) + " (" + String(result) + ")");
}

void sendHeartbeat() {
  if (!needToSendHeartbeat) return;
  needToSendHeartbeat = false;
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "heartbeat", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.serialNumber, DEVICE_SERIAL.c_str(), sizeof(sendBuffer.serialNumber));
  safeStringCopy(sendBuffer.result, "alive", sizeof(sendBuffer.result));
  
  sendBuffer.servoAngle = currentAngle;
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  int result = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[HEARTBEAT] ✓ Sent");
  } else {
    Serial.println("[HEARTBEAT] ✗ Failed: " + String(result));
  }
  
  lastHeartbeat = millis();
}

void sendStatusUpdate() {
  if (!needToSendStatus) return;
  needToSendStatus = false;
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "status", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.serialNumber, DEVICE_SERIAL.c_str(), sizeof(sendBuffer.serialNumber));
  
  String state = (currentState == DOOR_CLOSED) ? "closed" :
                 (currentState == OPENING) ? "opening" :
                 (currentState == OPEN) ? "open" : "closing";
  
  safeStringCopy(sendBuffer.result, state.c_str(), sizeof(sendBuffer.result));
  
  sendBuffer.servoAngle = currentAngle;
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  int result = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  Serial.println("[STATUS] " + state + " (" + String(result) + ")");
  
  lastStatusSent = millis();
}

void checkConnection() {
  if (gatewayOnline && (millis() - lastGatewayMessage > 180000)) {
    gatewayOnline = false;
    Serial.println("[TIMEOUT] Gateway offline");
  }
}

void printStatus() {
  Serial.println("\n=== ESP-01 STATUS ===");
  Serial.println("Gateway: " + String(gatewayOnline ? "ONLINE" : "OFFLINE"));
  Serial.println("Door: " + String(
    currentState == DOOR_CLOSED ? "CLOSED" :
    currentState == OPENING ? "OPENING" :
    currentState == OPEN ? "OPEN" : "CLOSING"
  ));
  Serial.println("Servo: " + String(currentAngle) + "°");
  Serial.println("TX/RX: " + String(messagesSent) + "/" + String(messagesReceived));
  Serial.println("Heap: " + String(ESP.getFreeHeap()));
  Serial.println("====================\n");
}

void loop() {
  // ✅ CRITICAL: Process all sending in main loop only
  processCommand();
  sendHeartbeat();
  sendStatusUpdate();
  
  // Heartbeat timer
  if (millis() - lastHeartbeat > 45000) {
    needToSendHeartbeat = true;
  }
  
  // Status timer
  if (millis() - lastStatusSent > 180000) {
    needToSendStatus = true;
  }
  
  // Connection check
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 15000) {
    checkConnection();
    lastCheck = millis();
  }
  
  // Status print
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 60000) {
    printStatus();
    lastPrint = millis();
  }
  
  // Keep servo stable
  if (!isMoving) {
    setServoAngle(currentAngle);
  }
  
  yield();
  delay(20);
}