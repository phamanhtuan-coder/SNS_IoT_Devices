#include <ESP8266WiFi.h>
#include <espnow.h>

// ===== DOOR CONFIGURATION =====
#define DOOR_ID 1         // Change this for each door (1-8)
#define OUTPUT_PIN 2      // Always use GPIO2 for output signal to UNO

// Serial number must match the one in ESP8266_Master.ino
const char* SERIAL_NUMBER = "SERL27JUN2501JYR2RKVVX08V40YMGTW"; // Update for each door

// ===== MASTER MAC ADDRESS =====
// ✅ FIXED: Updated with correct gateway MAC from logs
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6}; // Correct MAC!

// ===== COMPACT MESSAGE STRUCTURE =====
struct CompactMessage {
  char type[4];        // "CMD", "ACK", "HBT", "STS"
  char action[4];      // "OPN", "CLS", "TGL", "ALV", etc.
  int angle;           // Servo angle
  bool success;        // Success flag
  unsigned long ts;    // Timestamp
};

// ===== DOOR STATE =====
bool doorOpen = false;
int servoAngle = 0;
unsigned long lastCommand = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastStatusSent = 0;
unsigned long lastConnectionCheck = 0;

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
  
  // Send initial status
  delay(1000); // Give gateway time to initialize
  sendStatusMessage();
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
  if (len != sizeof(CompactMessage)) {
    Serial.println("Invalid message size: " + String(len));
    return;
  }
  
  CompactMessage msg;
  memcpy(&msg, data, len);
  
  String msgType = String(msg.type);
  String action = String(msg.action);
  
  Serial.println("📨 RX from Master: " + msgType + ":" + action);
  
  // Process command
  if (msgType == "CMD") {
    handleCommand(action);
  }
  // Process heartbeat
  else if (msgType == "HBT") {
    lastHeartbeat = millis();
    sendAckMessage("ALV", true);
    Serial.println("💓 Heartbeat received");
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
  CompactMessage msg;
  
  memset(&msg, 0, sizeof(msg));
  strncpy(msg.type, "ACK", 3);
  strncpy(msg.action, action.c_str(), 3);
  msg.angle = servoAngle;
  msg.success = success;
  msg.ts = millis();
  
  delay(10);
  int result = esp_now_send(gatewayMAC, (uint8_t*)&msg, sizeof(msg));
  
  if (result == 0) {
    Serial.println("📤 ACK sent: " + action + " (Angle: " + String(servoAngle) + ")");
  } else {
    Serial.println("❌ ACK send failed: " + String(result));
  }
}

// ===== SEND STATUS MESSAGE =====
void sendStatusMessage() {
  CompactMessage msg;
  
  memset(&msg, 0, sizeof(msg));
  strncpy(msg.type, "STS", 3);
  strncpy(msg.action, doorOpen ? "OPD" : "CLD", 3); // "OPD" = opened, "CLD" = closed
  msg.angle = servoAngle;
  msg.success = true;
  msg.ts = millis();
  
  delay(15);
  int result = esp_now_send(gatewayMAC, (uint8_t*)&msg, sizeof(msg));
  
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