#define DEVICE_ID 7
#define FIRMWARE_VERSION "4.0.0"

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Ticker.h>


// GATEWAY MAC ADDRESS
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVS2P6XBVF1P2E";

// ESP-01 PIN CONFIGURATION
#define SIGNAL_PIN 2  // GPIO2 - sends to UNO Receive

// ESP-NOW message structure
struct CompactMessage {
  char type[4];        // "CMD", "ACK", "HBT" (3 chars + null)
  char action[4];      // "OPN", "CLS", "TGL" (3 chars + null)  
  int angle;           // Current servo angle
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
  
  Serial.println("ESP-01 Door v4.0.0 (UNO Signal)");
  Serial.println("ID:" + String(DEVICE_ID));
  Serial.println("Serial:" + DEVICE_SERIAL);
  
  // Initialize signal pin
  pinMode(SIGNAL_PIN, OUTPUT);
  digitalWrite(SIGNAL_PIN, LOW);
  
  doorState = DOOR_CLOSED;
  
  // Setup ESP-NOW
  setupESPNow();
  
  Serial.println("Ready - Pin " + String(SIGNAL_PIN) + " to UNO");
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
  
  if (type == "CMD") {
    strncpy(pendingAction, rxBuffer.action, 3);
    pendingAction[3] = '\0';
    needSendResponse = true;
    
  } else if (type == "HBT") {
    needSendHeartbeat = true;
  }
}

void processCommand() {
  if (!needSendResponse) return;
  needSendResponse = false;
  
  String action = String(pendingAction);
  
  if (action == "OPN" || action == "CLS" || action == "TGL") {
    // Send device ID as pulse count to UNO
    sendPulsesToUNO(DEVICE_ID);
    
    // Update door state
    if (action == "OPN") {
      doorState = DOOR_OPEN;
    } else if (action == "CLS") {
      doorState = DOOR_CLOSED;
    } else if (action == "TGL") {
      doorState = (doorState == DOOR_CLOSED) ? DOOR_OPEN : DOOR_CLOSED;
    }
    
    pendingSuccess = true;
    Serial.println("Command: " + action + " -> " + String(DEVICE_ID) + " pulses");
    needSendStatus = true;
  }
  
  sendResponse();
}

void sendPulsesToUNO(int pulseCount) {
  Serial.println("Sending " + String(pulseCount) + " pulses to UNO");
  
  // Ensure pin starts HIGH
  digitalWrite(SIGNAL_PIN, HIGH);
  delay(100);
  
  // Send pulse train - each pulse is 250ms LOW, 100ms HIGH
  for (int i = 0; i < pulseCount; i++) {
    digitalWrite(SIGNAL_PIN, LOW);   // Pull pin LOW (active)
    delay(250);
    digitalWrite(SIGNAL_PIN, HIGH);  // Release pin HIGH (inactive)
    
    // Delay between pulses (except last)
    if (i < pulseCount - 1) {
      delay(100);
    }
  }
  
  // Ensure pin stays HIGH after transmission
  digitalWrite(SIGNAL_PIN, HIGH);
  Serial.println("Pulses sent successfully");
}

void sendResponse() {
  memset(&txBuffer, 0, sizeof(txBuffer));
  
  strncpy(txBuffer.type, "ACK", 3);
  strncpy(txBuffer.action, pendingAction, 3);
  txBuffer.angle = (doorState == DOOR_OPEN) ? 180 : 0;  // Simulated angle
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
  strncpy(txBuffer.action, "ALV", 3);
  txBuffer.angle = (doorState == DOOR_OPEN) ? 180 : 0;
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
  
  strncpy(txBuffer.type, "STS", 3);
  
  // State as 3-char codes
  if (doorState == DOOR_CLOSED) strncpy(txBuffer.action, "CLD", 3);
  else if (doorState == DOOR_OPENING) strncpy(txBuffer.action, "OPG", 3);
  else if (doorState == DOOR_OPEN) strncpy(txBuffer.action, "OPD", 3);
  else strncpy(txBuffer.action, "CLG", 3);
  
  txBuffer.angle = (doorState == DOOR_OPEN) ? 180 : 0;
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
  // Process commands and send responses
  processCommand();
  sendHeartbeat();
  sendStatus();
  
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
                   " Door:" + String(doorState) + 
                   " TX/RX:" + String(msgSent) + "/" + String(msgReceived));
    lastPrint = millis();
  }
  
  yield();
  delay(10);
}