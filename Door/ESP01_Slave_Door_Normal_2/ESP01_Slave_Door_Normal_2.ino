#include <ESP8266WiFi.h>
#include <espnow.h>

// ===== DOOR CONFIGURATION =====
#define DOOR_ID 2         // Door 1
#define OUTPUT_PIN 2      // Always use GPIO2 for output signal to UNO

// Serial number must match the one in ESP8266_Master.ino
const char* SERIAL_NUMBER = "SERL27JUN2501JYR2RKVR0SC7SJ8P8DD"; // Door 1

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
bool commandPending = false;

// ===== SETUP FUNCTION =====
void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Initialize pin as output
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW); // Start with door closed
  
  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init failed");
    delay(3000);
    ESP.restart();
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataReceived);
  
  Serial.println("ESP-01 Door " + String(DOOR_ID) + " Ready");
  Serial.println("Serial: " + String(SERIAL_NUMBER));
  Serial.println("Using GPIO" + String(OUTPUT_PIN) + " for output signal");
}

// ===== ESP-NOW DATA RECEIVED CALLBACK =====
void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(CompactMessage)) return;
  
  CompactMessage msg;
  memcpy(&msg, data, len);
  
  String msgType = String(msg.type);
  String action = String(msg.action);
  
  // Process command
  if (msgType == "CMD") {
    handleCommand(action);
  }
  // Process heartbeat
  else if (msgType == "HBT") {
    lastHeartbeat = millis();
    sendAckMessage("ALV", true);
  }
}

// ===== HANDLE COMMAND =====
void handleCommand(String action) {
  lastCommand = millis();
  
  // Toggle door state
  if (action == "TGL") {
    doorOpen = !doorOpen;
    servoAngle = doorOpen ? 180 : 0;
    
    // Set output pin HIGH for open, LOW for closed
    digitalWrite(OUTPUT_PIN, doorOpen ? HIGH : LOW);
    
    // Send acknowledgment
    sendAckMessage(doorOpen ? "OPN" : "CLS", true);
    
    Serial.println("Door " + String(DOOR_ID) + " toggled to " + 
                  (doorOpen ? "OPEN" : "CLOSED") + 
                  " - Signal: " + String(doorOpen ? "HIGH" : "LOW"));
  }
  // Open door
  else if (action == "OPN") {
    if (!doorOpen) {
      doorOpen = true;
      servoAngle = 180;
      digitalWrite(OUTPUT_PIN, HIGH);
      sendAckMessage("OPN", true);
      
      Serial.println("Door " + String(DOOR_ID) + " opened - Signal: HIGH");
    } else {
      sendAckMessage("OPN", true); // Already open
      Serial.println("Door " + String(DOOR_ID) + " already open");
    }
  }
  // Close door
  else if (action == "CLS") {
    if (doorOpen) {
      doorOpen = false;
      servoAngle = 0;
      digitalWrite(OUTPUT_PIN, LOW);
      sendAckMessage("CLS", true);
      
      Serial.println("Door " + String(DOOR_ID) + " closed - Signal: LOW");
    } else {
      sendAckMessage("CLS", true); // Already closed
      Serial.println("Door " + String(DOOR_ID) + " already closed");
    }
  }
}

// ===== SEND ACKNOWLEDGMENT MESSAGE =====
void sendAckMessage(String action, bool success) {
  CompactMessage msg;
  
  strcpy(msg.type, "ACK");
  strcpy(msg.action, action.c_str());
  msg.angle = servoAngle;
  msg.success = success;
  msg.ts = millis();
  
  // We don't send this directly - ESP-NOW Master will request status periodically
  commandPending = true;
}

// ===== MAIN LOOP =====
void loop() {
  // Very simple loop - ESP-01 just maintains state and output signal
  yield();
  delay(10);
}