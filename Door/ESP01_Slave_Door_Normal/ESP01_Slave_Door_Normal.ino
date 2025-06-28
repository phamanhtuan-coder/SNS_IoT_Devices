#define DEVICE_ID 1
#define FIRMWARE_VERSION "3.0.3-DEBUG"

#include <ESP8266WiFi.h>
#include <espnow.h>

// GATEWAY MAC ADDRESS - ESP Master thực tế: 48:3F:DA:1F:4A:A6
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};

// THIS ESP-01 MAC: 84:0D:8E:A4:91:58
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVVX08V40YMGTW";

// ESP-01 PIN CONFIGURATION
#define SERVO_PIN 2        // GPIO2 for servo

// MANUAL SERVO CONTROL VARIABLES
volatile bool servoActive = false;
volatile int targetAngle = 0;
volatile int currentAngle = 0;
const int SERVO_MIN_PULSE = 544;   // 0 degrees
const int SERVO_MAX_PULSE = 2400;  // 180 degrees

// ESP-NOW MESSAGE STRUCTURE - MATCHED WITH MASTER
struct ESPNowMessage {
  char messageType[16];
  char serialNumber[32];
  char action[16];
  int servoAngle;
  bool success;
  char result[32];
  unsigned long timestamp;
};

// DOOR STATE
enum DoorState { DOOR_CLOSED = 0, OPENING = 1, OPEN = 2, DOOR_CLOSING = 3 };
DoorState currentState = DOOR_CLOSED;
bool isMoving = false;
unsigned long lastHeartbeat = 0;
unsigned long lastStatusSent = 0;
bool gatewayOnline = false;
unsigned long lastGatewayMessage = 0;

// MEMORY PROTECTION
static ESPNowMessage receiveBuffer;
static ESPNowMessage sendBuffer;

// ✅ DEBUG COUNTERS
unsigned long debugCounter = 0;
unsigned long lastDebugPrint = 0;
unsigned long messagesSent = 0;
unsigned long messagesReceived = 0;
unsigned long sendFailures = 0;

void setup() {
  Serial.begin(115200);
  delay(3000); // Longer delay for stability
  
  Serial.println();
  Serial.println("==========================================");
  Serial.println("=== ESP-01 Door Controller ENHANCED DEBUG ===");
  Serial.println("==========================================");
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("Serial: " + DEVICE_SERIAL);
  Serial.println("Firmware: " + String(FIRMWARE_VERSION));
  
  // ✅ ENHANCED MAC DISPLAY
  Serial.println("\n=== MAC CONFIGURATION ===");
  Serial.println("My MAC: " + WiFi.macAddress());
  Serial.print("Gateway Target MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", gatewayMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.println("=========================");
  
  // ✅ ENHANCED PIN SETUP
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  Serial.println("[SERVO] Manual PWM servo on GPIO" + String(SERVO_PIN));
  
  // Initialize servo position
  Serial.println("[SERVO] Initializing position...");
  setServoAngle(0);
  currentAngle = 0;
  targetAngle = 0;
  currentState = DOOR_CLOSED;
  Serial.println("[SERVO] ✓ Initialized at 0°");
  
  // ✅ ENHANCED ESP-NOW SETUP
  setupESPNowEnhanced();
  
  Serial.println("\n[INIT] ✓ ESP-01 Ready - Enhanced Debug Mode");
  
  // ✅ ENHANCED INITIAL HEARTBEAT
  Serial.println("[INIT] Waiting 2s before sending initial heartbeat...");
  delay(2000);
  Serial.println("[INIT] Sending initial heartbeat...");
  sendHeartbeat();
  
  Serial.println("\n[INIT] ✓ Setup complete - Entering main loop");
  Serial.println("==========================================\n");
}

void setupESPNowEnhanced() {
  Serial.println("\n[ESP-NOW] Enhanced setup starting...");
  
  // ✅ CRITICAL: Enhanced WiFi setup for ESP-01
  Serial.println("[ESP-NOW] Setting WiFi mode...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(300); // Longer delay for ESP-01
  Serial.println("[ESP-NOW] WiFi mode: " + String(WiFi.getMode()));
  
  // ✅ ENHANCED CHANNEL SETUP
  Serial.println("[ESP-NOW] Setting channel...");
  wifi_set_channel(1);
  Serial.println("[ESP-NOW] Channel set to: " + String(wifi_get_channel()));
  
  // ✅ ENHANCED POWER MANAGEMENT
  Serial.println("[ESP-NOW] Disabling sleep...");
  wifi_set_sleep_type(NONE_SLEEP_T);
  Serial.println("[ESP-NOW] Sleep disabled");
  
  Serial.println("[ESP-NOW] Final MAC: " + WiFi.macAddress());
  
  // ✅ ENHANCED ESP-NOW INITIALIZATION
  Serial.println("[ESP-NOW] Initializing ESP-NOW...");
  int initResult = esp_now_init();
  Serial.println("[ESP-NOW] Init result: " + String(initResult));
  Serial.println("[ESP-NOW] Error codes: 0=OK, -1=FAIL, -2=ARG, -3=NO_MEM");
  
  if (initResult != 0) {
    Serial.println("[ESP-NOW] ✗ Init FAILED: " + String(initResult));
    Serial.println("[ESP-NOW] CRITICAL ERROR - Restarting in 5s...");
    delay(5000);
    ESP.restart();
  }
  
  Serial.println("[ESP-NOW] ✓ Init OK");
  
  // ✅ ENHANCED ROLE SETUP
  Serial.println("[ESP-NOW] Setting role...");
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  Serial.println("[ESP-NOW] Role set to COMBO");
  
  // ✅ ENHANCED CALLBACK REGISTRATION
  Serial.println("[ESP-NOW] Registering callbacks...");
  esp_now_register_send_cb(onDataSentEnhanced);
  esp_now_register_recv_cb(onDataReceivedEnhanced);
  Serial.println("[ESP-NOW] Callbacks registered");
  
  // ✅ ENHANCED PEER ADDITION
  Serial.println("[ESP-NOW] Adding gateway peer...");
  Serial.print("[ESP-NOW] Target Gateway MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", gatewayMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  int result = esp_now_add_peer(gatewayMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  Serial.println("[ESP-NOW] Peer add result: " + String(result));
  Serial.println("[ESP-NOW] Result codes: 0=OK, -1=FAIL, -2=ARG, -3=FULL, -4=NOT_FOUND, -5=EXIST");
  
  if (result == 0) {
    Serial.println("[ESP-NOW] ✓ Gateway peer added (Channel 1)");
  } else {
    Serial.println("[ESP-NOW] ✗ Peer add failed on channel 1: " + String(result));
    Serial.println("[ESP-NOW] Trying channel 0 fallback...");
    delay(100);
    result = esp_now_add_peer(gatewayMAC, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
    Serial.println("[ESP-NOW] Channel 0 result: " + String(result));
    
    if (result == 0) {
      Serial.println("[ESP-NOW] ✓ Gateway peer added (Channel 0 fallback)");
    } else {
      Serial.println("[ESP-NOW] ✗ ALL PEER ADD ATTEMPTS FAILED!");
      Serial.println("[ESP-NOW] ⚠️ CHECK GATEWAY MAC ADDRESS!");
    }
  }
  
  Serial.println("[ESP-NOW] ✓ Setup complete");
}

void onDataSentEnhanced(uint8_t *mac_addr, uint8_t sendStatus) {
  messagesSent++;
  
  Serial.print("[ESP-NOW] SEND #" + String(messagesSent) + " to ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  
  if (sendStatus == 0) {
    Serial.println(" ✓ SUCCESS");
  } else {
    sendFailures++;
    Serial.println(" ✗ FAILED (" + String(sendStatus) + ")");
    Serial.println("[ESP-NOW] Send failures: " + String(sendFailures) + "/" + String(messagesSent));
  }
}

void onDataReceivedEnhanced(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  messagesReceived++;
  
  Serial.println("\n[ESP-NOW] ================================");
  Serial.println("[ESP-NOW] ✓ MESSAGE RECEIVED #" + String(messagesReceived));
  Serial.print("[ESP-NOW] From MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.println("[ESP-NOW] Data length: " + String(len));
  Serial.println("[ESP-NOW] Expected length: " + String(sizeof(ESPNowMessage)));
  
  // ✅ ENHANCED MAC VALIDATION
  bool isFromGateway = (memcmp(mac_addr, gatewayMAC, 6) == 0);
  Serial.println("[ESP-NOW] From Gateway? " + String(isFromGateway ? "YES" : "NO"));
  
  if (!isFromGateway) {
    Serial.print("[ESP-NOW] Expected Gateway MAC: ");
    for (int i = 0; i < 6; i++) {
      Serial.printf("%02X", gatewayMAC[i]);
      if (i < 5) Serial.print(":");
    }
    Serial.println();
    Serial.println("[ESP-NOW] ✗ Unknown sender - MESSAGE IGNORED");
    Serial.println("[ESP-NOW] ================================\n");
    return;
  }
  
  // ✅ ENHANCED SIZE VALIDATION
  if (len != sizeof(ESPNowMessage)) {
    Serial.println("[ESP-NOW] ✗ Invalid message size - MESSAGE IGNORED");
    Serial.println("[ESP-NOW] ================================\n");
    return;
  }
  
  // ✅ ENHANCED MESSAGE PROCESSING
  Serial.println("[ESP-NOW] ✓ Message validation passed");
  Serial.println("[ESP-NOW] Copying message data...");
  
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  memcpy(&receiveBuffer, incomingData, len);
  
  // ✅ UPDATE CONNECTION STATUS
  gatewayOnline = true;
  lastGatewayMessage = millis();
  Serial.println("[ESP-NOW] Gateway connection updated");
  
  String msgType = String(receiveBuffer.messageType);
  Serial.println("[ESP-NOW] Message type: '" + msgType + "'");
  Serial.println("[ESP-NOW] Serial number: '" + String(receiveBuffer.serialNumber) + "'");
  Serial.println("[ESP-NOW] Action: '" + String(receiveBuffer.action) + "'");
  Serial.println("[ESP-NOW] Servo angle: " + String(receiveBuffer.servoAngle));
  Serial.println("[ESP-NOW] Success: " + String(receiveBuffer.success));
  Serial.println("[ESP-NOW] Result: '" + String(receiveBuffer.result) + "'");
  Serial.println("[ESP-NOW] Timestamp: " + String(receiveBuffer.timestamp));
  
  // ✅ ENHANCED MESSAGE ROUTING
  if (msgType == "command") {
    String action = String(receiveBuffer.action);
    Serial.println("[ESP-NOW] ✓ COMMAND MESSAGE: " + action);
    Serial.println("[ESP-NOW] ================================\n");
    handleCommandSafe(action);
    
  } else if (msgType == "heartbeat") {
    Serial.println("[ESP-NOW] ✓ HEARTBEAT MESSAGE");
    Serial.println("[ESP-NOW] ================================\n");
    Serial.println("[HEARTBEAT] Gateway ping received");
    delay(50);
    sendHeartbeat();
    
  } else {
    Serial.println("[ESP-NOW] ✗ UNKNOWN MESSAGE TYPE: '" + msgType + "'");
    Serial.println("[ESP-NOW] ================================\n");
  }
}

void handleCommandSafe(String action) {
  Serial.println("\n[CMD] ==============================");
  Serial.println("[CMD] Processing command: '" + action + "'");
  Serial.println("[CMD] Current state: " + String(currentState));
  Serial.println("[CMD] Is moving: " + String(isMoving));
  
  bool success = false;
  String result = "";
  
  if (isMoving) {
    result = "Door busy";
    success = false;
    Serial.println("[CMD] ✗ Door is currently moving");
    
  } else if (action == "open_door" && currentState == DOOR_CLOSED) {
    Serial.println("[CMD] ✓ Opening door...");
    openDoorSafe();
    success = true;
    result = "Opening";
    
  } else if (action == "close_door" && currentState == OPEN) {
    Serial.println("[CMD] ✓ Closing door...");
    closeDoorSafe();
    success = true;
    result = "Closing";
    
  } else if (action == "toggle_door") {
    Serial.println("[CMD] ✓ Toggling door...");
    if (currentState == DOOR_CLOSED) {
      Serial.println("[CMD] Door closed -> Opening");
      openDoorSafe();
      result = "Toggle open";
    } else if (currentState == OPEN) {
      Serial.println("[CMD] Door open -> Closing");
      closeDoorSafe();
      result = "Toggle close";
    } else {
      Serial.println("[CMD] Door in motion -> Cannot toggle");
      result = "Door moving";
    }
    success = true;
    
  } else {
    Serial.println("[CMD] ✗ Invalid command or wrong state");
    Serial.println("[CMD] Action: '" + action + "', State: " + String(currentState));
    result = "Invalid/busy";
    success = false;
  }
  
  Serial.println("[CMD] Result: " + String(success) + " (" + result + ")");
  Serial.println("[CMD] Sending response...");
  
  delay(100);
  sendCommandResponse(action, success, result);
  
  Serial.println("[CMD] ==============================\n");
}

void openDoorSafe() {
  if (isMoving) return;
  
  Serial.println("[DOOR] ✓ Opening door...");
  currentState = OPENING;
  isMoving = true;
  
  moveServoSafe(180);
  
  currentState = OPEN;
  isMoving = false;
  Serial.println("[DOOR] ✓ Door opened successfully");
  
  sendStatusUpdate();
}

void closeDoorSafe() {
  if (isMoving) return;
  
  Serial.println("[DOOR] ✓ Closing door...");
  currentState = DOOR_CLOSING;
  isMoving = true;
  
  moveServoSafe(0);
  
  currentState = DOOR_CLOSED;
  isMoving = false;
  Serial.println("[DOOR] ✓ Door closed successfully");
  
  sendStatusUpdate();
}

void moveServoSafe(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  
  Serial.println("[SERVO] Moving from " + String(currentAngle) + "° to " + String(angle) + "°");
  targetAngle = angle;
  
  int step = (targetAngle > currentAngle) ? 5 : -5;
  
  while (currentAngle != targetAngle) {
    if (abs(currentAngle - targetAngle) < 5) {
      currentAngle = targetAngle;
    } else {
      currentAngle += step;
    }
    
    setServoAngle(currentAngle);
    delay(20);
    yield();
  }
  
  Serial.println("[SERVO] ✓ Servo moved to " + String(currentAngle) + "°");
}

void setServoAngle(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  
  int pulseWidth = map(angle, 0, 180, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
  
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(SERVO_PIN, LOW);
  delayMicroseconds(20000 - pulseWidth);
}

void sendCommandResponse(String action, bool success, String result) {
  Serial.println("[RESP] Preparing command response...");
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  strncpy(sendBuffer.messageType, "cmd_resp", 7);
  strncpy(sendBuffer.serialNumber, DEVICE_SERIAL.c_str(), 15);
  strncpy(sendBuffer.action, action.c_str(), 7);
  sendBuffer.servoAngle = currentAngle;
  sendBuffer.success = success;
  strncpy(sendBuffer.result, result.c_str(), 15);
  sendBuffer.timestamp = millis();
  
  Serial.println("[RESP] Message prepared:");
  Serial.println("[RESP] - Type: " + String(sendBuffer.messageType));
  Serial.println("[RESP] - Action: " + String(sendBuffer.action));
  Serial.println("[RESP] - Success: " + String(sendBuffer.success));
  Serial.println("[RESP] - Result: " + String(sendBuffer.result));
  Serial.println("[RESP] - Angle: " + String(sendBuffer.servoAngle));
  
  delay(10);
  int result_code = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  Serial.println("[RESP] Send result: " + String(result_code));
  if (result_code == 0) {
    Serial.println("[RESP] ✓ Response sent: " + action + " = " + String(success));
  } else {
    Serial.println("[RESP] ✗ Send failed: " + String(result_code));
  }
}

void sendHeartbeat() {
  Serial.println("[HEARTBEAT] Preparing heartbeat...");
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  strncpy(sendBuffer.messageType, "heartbeat", 7);
  strncpy(sendBuffer.serialNumber, DEVICE_SERIAL.c_str(), 15);
  sendBuffer.servoAngle = currentAngle;
  sendBuffer.success = true;
  strncpy(sendBuffer.result, "alive", 15);
  sendBuffer.timestamp = millis();
  
  Serial.println("[HEARTBEAT] Message prepared:");
  Serial.println("[HEARTBEAT] - Type: " + String(sendBuffer.messageType));
  Serial.println("[HEARTBEAT] - Serial: " + String(sendBuffer.serialNumber));
  Serial.println("[HEARTBEAT] - Angle: " + String(sendBuffer.servoAngle));
  
  delay(20);
  int result = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  Serial.println("[HEARTBEAT] Send result: " + String(result));
  if (result == 0) {
    Serial.println("[HEARTBEAT] ✓ Heartbeat sent (angle: " + String(currentAngle) + "°)");
  } else {
    Serial.println("[HEARTBEAT] ✗ Heartbeat failed: " + String(result));
  }
  
  lastHeartbeat = millis();
}

void sendStatusUpdate() {
  Serial.println("[STATUS] Sending status update...");
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  strncpy(sendBuffer.messageType, "status", 7);
  strncpy(sendBuffer.serialNumber, DEVICE_SERIAL.c_str(), 15);
  sendBuffer.servoAngle = currentAngle;
  sendBuffer.success = true;
  
  String state = (currentState == DOOR_CLOSED) ? "closed" :
                 (currentState == OPENING) ? "opening" :
                 (currentState == OPEN) ? "open" : "closing";
  
  strncpy(sendBuffer.result, state.c_str(), 15);
  sendBuffer.timestamp = millis();
  
  Serial.println("[STATUS] Status: " + state + " @ " + String(currentAngle) + "°");
  
  delay(15);
  int result = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[STATUS] ✓ Status sent: " + state);
  } else {
    Serial.println("[STATUS] ✗ Status failed: " + String(result));
  }
  
  lastStatusSent = millis();
}

void checkGatewayConnection() {
  unsigned long now = millis();
  
  if (gatewayOnline && (now - lastGatewayMessage > 180000)) {
    gatewayOnline = false;
    Serial.println("[TIMEOUT] Gateway connection lost (3min timeout)");
  }
}

void printStatus() {
  Serial.println("\n=== ESP-01 ENHANCED STATUS ===");
  Serial.println("Serial: " + DEVICE_SERIAL);
  Serial.println("Gateway: " + String(gatewayOnline ? "✓ ONLINE" : "✗ OFFLINE"));
  if (lastGatewayMessage > 0) {
    Serial.println("Last Gateway msg: " + String((millis() - lastGatewayMessage) / 1000) + "s ago");
  }
  Serial.println("Door: " + String(
    currentState == DOOR_CLOSED ? "CLOSED" :
    currentState == OPENING ? "OPENING" :
    currentState == OPEN ? "OPEN" : "CLOSING"
  ));
  Serial.println("Servo: " + String(currentAngle) + "°");
  Serial.println("Moving: " + String(isMoving ? "Yes" : "No"));
  Serial.println("Messages sent: " + String(messagesSent));
  Serial.println("Messages received: " + String(messagesReceived));
  Serial.println("Send failures: " + String(sendFailures));
  Serial.println("Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("Uptime: " + String(millis() / 1000) + "s");
  Serial.println("Channel: " + String(wifi_get_channel()));
  Serial.println("=============================\n");
}

void loop() {
  debugCounter++;
  
  // Heartbeat timer
  if (millis() - lastHeartbeat > 45000) {
    Serial.println("[LOOP] Heartbeat timer triggered");
    sendHeartbeat();
  }
  
  // Status update timer
  if (millis() - lastStatusSent > 180000) {
    Serial.println("[LOOP] Status update timer triggered");
    sendStatusUpdate();
  }
  
  // Gateway connection check
  static unsigned long lastGatewayCheck = 0;
  if (millis() - lastGatewayCheck > 15000) {
    checkGatewayConnection();
    lastGatewayCheck = millis();
  }
  
  // Enhanced status print
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 60000) { // Every 60s instead of 120s
    printStatus();
    lastStatusPrint = millis();
  }
  
  // Keep servo position stable
  if (!isMoving && (millis() % 100 == 0)) {
    setServoAngle(currentAngle);
  }
  
  // ✅ Enhanced debug info every 20 seconds
  if (millis() - lastDebugPrint > 20000) {
    Serial.println("[DEBUG] Loop #" + String(debugCounter) + " - ESP-01 alive");
    Serial.println("[DEBUG] Gateway online: " + String(gatewayOnline));
    Serial.println("[DEBUG] Messages: TX=" + String(messagesSent) + " RX=" + String(messagesReceived));
    lastDebugPrint = millis();
  }
  
  yield();
  delay(20);
}