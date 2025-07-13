// ESP01 Slave - With SoftwareSerial Communication
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

#define EEPROM_SIZE 64
#define ADDR_DOOR_STATE 0
#define ADDR_INIT_FLAG 12

#define DOOR_ID 8
#define OUTPUT_PIN 2  // Will now be used for SoftwareSerial TX

// Create software serial instance (TX only on GPIO2)
SoftwareSerial commandSerial(NULL, OUTPUT_PIN); // TX only, no RX needed

const char* SERIAL_NUMBER = "SERL27JUN2501JYR2RKVTH6PWR9ETXC2";
uint8_t gatewayMAC[6] = {0x48, 0x3F, 0xDA, 0x1F, 0x4A, 0xA6};

struct __attribute__((packed)) CompactMessage {
  char type[4];
  char action[4];
  int angle;
  bool success;
  unsigned long ts;
};

bool doorOpen = false;
int servoAngle = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastStatusSent = 0;
unsigned long lastCommand = 0;

CompactMessage sendBuffer;
CompactMessage receiveBuffer;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Initialize software serial for communication with UNO
  commandSerial.begin(9600); // Lower baud rate for better reliability
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(1);
  
  if (esp_now_init() != 0) {
    Serial.println("ESPNOW FAIL");
    delay(3000);
    ESP.restart();
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataReceived);
  esp_now_register_send_cb(onDataSent);
  
  if (esp_now_add_peer(gatewayMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0) != 0) {
    Serial.println("PEER FAIL");
    delay(2000);
    ESP.restart();
  }
  
  Serial.println("D" + String(DOOR_ID) + " READY - SoftwareSerial Mode");
  Serial.println("SN:" + String(SERIAL_NUMBER));
  
  EEPROM.begin(EEPROM_SIZE);
  loadDoorState();
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  
  delay(1000);
  sendSimpleOnline();
  lastHeartbeat = millis();
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.println(sendStatus == 0 ? "TX_OK" : "TX_FAIL");
}

void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len) {
  // Command cooldown
  if (millis() - lastCommand < 3000) {
    Serial.println("CMD_COOLDOWN");
    return;
  }
  
  if (len != sizeof(CompactMessage) || memcmp(mac, gatewayMAC, 6) != 0) {
    return;
  }
  
  // Quick memory copy without interrupts
  memcpy(&receiveBuffer, data, len);
  receiveBuffer.type[3] = '\0';
  receiveBuffer.action[3] = '\0';
  
  String msgType = String(receiveBuffer.type);
  String action = String(receiveBuffer.action);
  
  Serial.println("RX:" + msgType + ":" + action);
  
  if (msgType == "CMD") {
    lastCommand = millis();
    handleCommand(action);
  } else if (msgType == "HBT") {
    lastHeartbeat = millis();
  }
}

void handleCommand(String action) {
  Serial.println("CMD:" + action);
  
  bool wasOpen = doorOpen;
  
  if (action == "TGL") {
    doorOpen = !doorOpen;
    servoAngle = doorOpen ? 180 : 0;
    sendSerialCommand("D" + String(DOOR_ID) + ":" + String(doorOpen ? "1" : "0"));
    Serial.println("TGL->" + String(doorOpen ? "OPEN" : "CLOSED"));
    
  } else if (action == "OPN") {
    doorOpen = true;
    servoAngle = 180;
    sendSerialCommand("D" + String(DOOR_ID) + ":1");
    Serial.println("OPEN_FORCE");
    
  } else if (action == "CLS") {
    doorOpen = false;
    servoAngle = 0;
    sendSerialCommand("D" + String(DOOR_ID) + ":0");
    Serial.println("CLOSE_FORCE");
    
  } else if (action == "CFG") {
    sendConfigMessage();
    return;
  }
  
  // Send ACK
  sendAckMessage(doorOpen ? "OPN" : "CLS", true);
  
  // Save state
  if (wasOpen != doorOpen) {
    saveDoorState();
  }
}

void sendSerialCommand(String command) {
  // Send command to UNO Receiver via SoftwareSerial
  commandSerial.println(command);
  Serial.println("SERIAL_SENT:" + command);
  
  // Add a small delay to ensure command is fully transmitted
  delay(50);
}

void sendAckMessage(String action, bool success) {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "ACK", 3);
  strncpy(sendBuffer.action, action.c_str(), 3);
  sendBuffer.angle = servoAngle;
  sendBuffer.success = success;
  sendBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  Serial.println("ACK:" + action);
}

void sendStatusMessage() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "STS", 3);
  String statusAction = doorOpen ? "OPD" : "CLD";
  strncpy(sendBuffer.action, statusAction.c_str(), 3);
  sendBuffer.angle = servoAngle;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  lastStatusSent = millis();
  Serial.println("STS:" + statusAction);
}

void sendConfigMessage() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "CFG", 3);
  strncpy(sendBuffer.action, "ANG", 3);
  sendBuffer.angle = (0 << 8) | 180; // Closed angle | Open angle
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
}

void sendSimpleOnline() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "ONL", 3);
  strncpy(sendBuffer.action, "RDY", 3);
  sendBuffer.angle = DOOR_ID;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
}

void sendAliveMessage() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "HBT", 3);
  strncpy(sendBuffer.action, "ALV", 3);
  sendBuffer.angle = servoAngle;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
}

void saveDoorState() {
  EEPROM.put(ADDR_DOOR_STATE, doorOpen);
  EEPROM.put(ADDR_INIT_FLAG, 0xAA);
  EEPROM.commit();
}

void loadDoorState() {
  uint8_t initFlag;
  EEPROM.get(ADDR_INIT_FLAG, initFlag);
  
  if (initFlag == 0xAA) {
    EEPROM.get(ADDR_DOOR_STATE, doorOpen);
    servoAngle = doorOpen ? 180 : 0;
    Serial.println("STATE_LOADED");
  } else {
    doorOpen = false;
    servoAngle = 0;
    Serial.println("DEFAULT_STATE");
    saveDoorState();
  }
}

void loop() {
  // Send status every 30 seconds
  if (millis() - lastStatusSent > 30000) {
    sendStatusMessage();
  }
  
  // Send alive every 60 seconds
  static unsigned long lastAlive = 0;
  if (millis() - lastAlive > 60000) {
    sendAliveMessage();
    lastAlive = millis();
  }
  
  // Check heartbeat timeout (3 minutes)
  static unsigned long lastConnectionCheck = 0;
  if (millis() - lastConnectionCheck > 10000) {
    if (millis() - lastHeartbeat > 180000) {
      Serial.println("RESTART");
      delay(1000);
      ESP.restart();
    }
    lastConnectionCheck = millis();
  }
  
  yield();
  delay(50);
}