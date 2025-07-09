#include <ESP8266WiFi.h>
#include <espnow.h>

// ===== DOOR CONFIGURATION =====
#define DOOR_ID 4         // Change this for each door (1-8)
#define OUTPUT_PIN 2      // Always use GPIO2 for output signal to UNO

// Serial number must match the one in ESP8266_Master.ino
const char* SERIAL_NUMBER = "SERL27JUN2501JYR2RKVSE2RW7KQ4KMP"; // Update for each door

// ===== MASTER MAC ADDRESS =====
// ✅ FIXED: Updated with correct gateway MAC from logs
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6}; // Correct MAC!

// ===== COMPACT MESSAGE STRUCTURE =====
struct CompactMessage {
  char type[4];        // "CMD", "ACK", "HBT", "STS" - NULL terminated
  char action[4];      // "OPN", "CLS", "TGL", "ALV", etc. - NULL terminated
  int angle;           // Servo angle
  bool success;        // Success flag
  unsigned long ts;    // Timestamp
} __attribute__((packed)); // Ensure consistent packing

// ===== DOOR STATE =====
bool doorOpen = false;
int servoAngle = 0;
unsigned long lastCommand = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastStatusSent = 0;
unsigned long lastConnectionCheck = 0;

// ===== BUFFERS FOR SAFE MESSAGE HANDLING =====
CompactMessage sendBuffer;
CompactMessage receiveBuffer;

// ===== SETUP FUNCTION =====
void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Initialize pin as output
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, HIGH); // Start HIGH (Arduino reads HIGH = door closed with pullup)
  
  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(1); // Same channel as gateway
  
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init failed - restarting...");
    delay(3000);
    ESP.restart();
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataReceived);
  esp_now_register_send_cb(onDataSent);
  
  // ✅ ADD MASTER AS PEER
  int addPeerResult = esp_now_add_peer(gatewayMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  if (addPeerResult == 0) {
    Serial.println("✅ Master peer added successfully");
  } else {
    Serial.println("❌ Failed to add master peer: " + String(addPeerResult));
    delay(2000);
    ESP.restart(); // Restart if can't add peer
  }
  
  Serial.println("ESP-01 Door " + String(DOOR_ID) + " Ready");
  Serial.println("Serial: " + String(SERIAL_NUMBER));
  Serial.println("Using GPIO" + String(OUTPUT_PIN) + " for output signal");
  Serial.println("My MAC: " + WiFi.macAddress());
  Serial.print("Master MAC: ");
  for (int i = 0; i < 6; i++) {
    if (i > 0) Serial.print(":");
    Serial.printf("%02X", gatewayMAC[i]);
  }
  Serial.println();
  
  // Initialize buffers
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  
  // Send initial status
  delay(1000); // Give gateway time to initialize
  sendStatusMessage();
  lastHeartbeat = millis(); // Initialize heartbeat timer
}

// ===== ESP-NOW DATA SENT CALLBACK =====
void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    Serial.println("✅ TX OK to Master");
  } else {
    Serial.println("❌ TX FAIL to Master: " + String(sendStatus));
  }
}

// ===== ESP-NOW DATA RECEIVED CALLBACK =====
void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len) {
  // ✅ CRITICAL FIX: Disable interrupts during processing
  noInterrupts();
  
  // ✅ FIXED: Validate message size and MAC
  if (len != sizeof(CompactMessage)) {
    Serial.println("❌ Invalid message size: " + String(len) + " expected: " + String(sizeof(CompactMessage)));
    interrupts();
    return;
  }
  
  // ✅ FIXED: Verify sender is gateway
  if (memcmp(mac, gatewayMAC, 6) != 0) {
    Serial.println("❌ Message from unknown sender");
    interrupts();
    return;
  }
  
  // ✅ CRITICAL FIX: Use local buffer to prevent corruption
  CompactMessage localMsg;
  memset(&localMsg, 0, sizeof(localMsg));
  memcpy(&localMsg, data, len);
  
  // ✅ CRITICAL FIX: Force null termination with bounds check
  localMsg.type[3] = '\0';
  localMsg.action[3] = '\0';
  
  // Re-enable interrupts
  interrupts();
  
  // ✅ SAFE: Process with local variables only
  char msgTypeArray[4];
  char actionArray[4];
  strncpy(msgTypeArray, localMsg.type, 3);
  strncpy(actionArray, localMsg.action, 3);
  msgTypeArray[3] = '\0';
  actionArray[3] = '\0';
  
  String msgType = String(msgTypeArray);
  String action = String(actionArray);
  
  Serial.println("📨 RX from Master: " + msgType + ":" + action);
  
  // Process command
  if (msgType == "CMD") {
    // Copy to global buffer for command processing
    memcpy(&receiveBuffer, &localMsg, sizeof(localMsg));
    handleCommand(action);
  }
  // Process heartbeat - MINIMAL processing
  else if (msgType == "HBT") {
    lastHeartbeat = millis();
    Serial.println("💓 Heartbeat received");
    
    // Send simple ACK without buffer operations
    delay(10);
    CompactMessage ackMsg;
    memset(&ackMsg, 0, sizeof(ackMsg));
    strncpy(ackMsg.type, "ACK", 3);
    strncpy(ackMsg.action, "ALV", 3);
    ackMsg.type[3] = '\0';
    ackMsg.action[3] = '\0';
    ackMsg.success = true;
    ackMsg.ts = millis();
    
    esp_now_send(gatewayMAC, (uint8_t*)&ackMsg, sizeof(ackMsg));
  }
}

// ===== HANDLE COMMAND =====
void handleCommand(String action) {
  lastCommand = millis();
  bool wasOpen = doorOpen;
  
  Serial.println("🔧 Processing command: " + action);
  
  // Toggle door state
  if (action == "TGL") {
    doorOpen = !doorOpen;
    servoAngle = doorOpen ? 180 : 0;
    
    // ✅ FIXED LOGIC: Arduino uses INPUT_PULLUP
    // LOW = ESP-01 active = door open
    // HIGH = ESP-01 inactive = door closed  
    digitalWrite(OUTPUT_PIN, doorOpen ? LOW : HIGH);
    
    // Send acknowledgment
    sendAckMessage(doorOpen ? "OPN" : "CLS", true);
    
    Serial.println("🚪 Door " + String(DOOR_ID) + " toggled to " + 
                  (doorOpen ? "OPEN" : "CLOSED") + 
                  " - Signal: " + String(doorOpen ? "LOW" : "HIGH"));
  }
  // Open door
  else if (action == "OPN") {
    if (!doorOpen) {
      doorOpen = true;
      servoAngle = 180;
      digitalWrite(OUTPUT_PIN, LOW);  // LOW = door open
      sendAckMessage("OPN", true);
      
      Serial.println("🚪 Door " + String(DOOR_ID) + " opened - Signal: LOW");
    } else {
      sendAckMessage("OPN", true); // Already open
      Serial.println("ℹ️ Door " + String(DOOR_ID) + " already open");
    }
  }
  // Close door
  else if (action == "CLS") {
    if (doorOpen) {
      doorOpen = false;
      servoAngle = 0;
      digitalWrite(OUTPUT_PIN, HIGH); // HIGH = door closed
      sendAckMessage("CLS", true);
      
      Serial.println("🚪 Door " + String(DOOR_ID) + " closed - Signal: HIGH");
    } else {
      sendAckMessage("CLS", true); // Already closed
      Serial.println("ℹ️ Door " + String(DOOR_ID) + " already closed");
    }
  }
  
  // Send status update if state changed
  if (wasOpen != doorOpen) {
    delay(100); // Small delay before status
    sendStatusMessage();
  }
}

// ===== SEND ACKNOWLEDGMENT MESSAGE =====
void sendAckMessage(String action, bool success) {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  // ✅ FIXED: Safe string copying with bounds checking
  strncpy(sendBuffer.type, "ACK", 3);
  sendBuffer.type[3] = '\0';
  
  strncpy(sendBuffer.action, action.c_str(), 3);
  sendBuffer.action[3] = '\0';
  
  sendBuffer.angle = servoAngle;
  sendBuffer.success = success;
  sendBuffer.ts = millis();
  
  delay(10);
  int result = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("📤 ACK sent: " + action + " (Angle: " + String(servoAngle) + ")");
  } else {
    Serial.println("❌ ACK send failed: " + String(result));
  }
}

// ===== SEND STATUS MESSAGE =====
void sendStatusMessage() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  // ✅ FIXED: Safe string copying with bounds checking
  strncpy(sendBuffer.type, "STS", 3);
  sendBuffer.type[3] = '\0';
  
  String statusAction = doorOpen ? "OPD" : "CLD"; // "OPD" = opened, "CLD" = closed
  strncpy(sendBuffer.action, statusAction.c_str(), 3);
  sendBuffer.action[3] = '\0';
  
  sendBuffer.angle = servoAngle;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  delay(15);
  int result = esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("📊 STATUS sent: " + String(doorOpen ? "OPEN" : "CLOSED"));
  } else {
    Serial.println("❌ STATUS send failed: " + String(result));
  }
  
  lastStatusSent = millis();
}

// ===== MAIN LOOP =====
void loop() {
  // Send periodic status every 30 seconds
  if (millis() - lastStatusSent > 30000) {
    sendStatusMessage();
  }
  
  // Check connection health every 5 seconds
  if (millis() - lastConnectionCheck > 5000) {
    if (millis() - lastHeartbeat > 120000) { // 2 minutes without heartbeat
      Serial.println("💀 No heartbeat from master for 2 minutes - restarting...");
      delay(1000);
      ESP.restart();
    } else if (millis() - lastHeartbeat > 60000) { // 1 minute warning
      Serial.println("⚠️ No heartbeat from master for 1 minute");
    }
    lastConnectionCheck = millis();
  }
  
  yield();
  delay(50);
}