#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

#define EEPROM_SIZE 64
#define ADDR_DOOR_STATE 0
#define ADDR_SERVO_CLOSED_ANGLE 4
#define ADDR_SERVO_OPEN_ANGLE 8
#define ADDR_INIT_FLAG 12

#define DOOR_ID 4
#define OUTPUT_PIN 2
#define DEFAULT_CLOSED_ANGLE 0
#define DEFAULT_OPEN_ANGLE 180

const char* SERIAL_NUMBER = "SERL27JUN2501JYR2RKVSE2RW7KQ4KMP";
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
int servoClosedAngle = DEFAULT_CLOSED_ANGLE;
int servoOpenAngle = DEFAULT_OPEN_ANGLE;
unsigned long lastHeartbeat = 0;
unsigned long lastStatusSent = 0;
unsigned long lastConnectionCheck = 0;

CompactMessage sendBuffer;
CompactMessage receiveBuffer;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, HIGH);
  
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
  
  Serial.println("D" + String(DOOR_ID) + " READY");
  Serial.println("SN:" + String(SERIAL_NUMBER));
  Serial.println("MAC:" + WiFi.macAddress());
  
  EEPROM.begin(EEPROM_SIZE);
  loadDoorState();
  digitalWrite(OUTPUT_PIN, doorOpen ? LOW : HIGH);
  
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  memset(&receiveBuffer, 0, sizeof(receiveBuffer));
  
  delay(1000);
  sendEnhancedCapabilities();
  lastHeartbeat = millis();
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.println(sendStatus == 0 ? "TX OK" : "TX FAIL");
}

void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len) {
  noInterrupts();
  
  if (len != sizeof(CompactMessage) || memcmp(mac, gatewayMAC, 6) != 0) {
    interrupts();
    return;
  }
  
  CompactMessage localMsg;
  memset(&localMsg, 0, sizeof(localMsg));
  memcpy(&localMsg, data, len);
  localMsg.type[3] = '\0';
  localMsg.action[3] = '\0';
  
  interrupts();
  
  String msgType = String(localMsg.type);
  String action = String(localMsg.action);
  
  Serial.println("RX:" + msgType + ":" + action);
  
  if (msgType == "CMD") {
    memcpy(&receiveBuffer, &localMsg, sizeof(localMsg));
    handleCommand(action);
  } else if (msgType == "HBT") {
    lastHeartbeat = millis();
  }
}

void handleCommand(String action) {
  bool wasOpen = doorOpen;
  
  if (action == "TGL") {
    doorOpen = !doorOpen;
    servoAngle = doorOpen ? servoOpenAngle : servoClosedAngle;
    digitalWrite(OUTPUT_PIN, doorOpen ? LOW : HIGH);
    delay(100);
    sendAckMessage(doorOpen ? "OPN" : "CLS", true);
    Serial.println("TGL->" + String(doorOpen ? "OPEN" : "CLOSED"));
    
  } else if (action == "OPN") {
    if (!doorOpen) {
      doorOpen = true;
      servoAngle = servoOpenAngle;
      digitalWrite(OUTPUT_PIN, LOW);
      delay(100);
      sendAckMessage("OPN", true);
      Serial.println("OPEN");
    } else {
      sendAckMessage("OPN", true);
    }
    
  } else if (action == "CLS") {
    if (doorOpen) {
      doorOpen = false;
      servoAngle = servoClosedAngle;
      digitalWrite(OUTPUT_PIN, HIGH);
      delay(100);
      sendAckMessage("CLS", true);
      Serial.println("CLOSE");
    } else {
      sendAckMessage("CLS", true);
    }
    
  } else if (action == "CFG") {
    sendConfigMessage();
  }
  
  if (wasOpen != doorOpen) {
    saveDoorState();
    delay(200);
    sendStatusMessage();
  }
}

void sendAckMessage(String action, bool success) {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "ACK", 3);
  strncpy(sendBuffer.action, action.c_str(), 3);
  sendBuffer.angle = servoAngle;
  sendBuffer.success = success;
  sendBuffer.ts = millis();
  
  delay(10);
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
}

void sendStatusMessage() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "STS", 3);
  String statusAction = doorOpen ? "OPD" : "CLD";
  strncpy(sendBuffer.action, statusAction.c_str(), 3);
  sendBuffer.angle = servoAngle;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  delay(15);
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  lastStatusSent = millis();
}

void sendConfigMessage() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "CFG", 3);
  strncpy(sendBuffer.action, "ANG", 3);
  sendBuffer.angle = (servoClosedAngle << 8) | servoOpenAngle;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  delay(15);
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
}

void sendAliveMessage() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "HBT", 3);
  strncpy(sendBuffer.action, "ALV", 3);
  sendBuffer.angle = servoAngle;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  delay(15);
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
}

void sendDeviceOnline() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  strncpy(sendBuffer.type, "ONL", 3);
  strncpy(sendBuffer.action, "CAP", 3);
  sendBuffer.angle = DOOR_ID;
  sendBuffer.success = true;
  sendBuffer.ts = millis();
  
  delay(15);
  esp_now_send(gatewayMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
}

void sendEnhancedCapabilities() {
  sendDeviceOnline();
  delay(500);
  sendConfigMessage();
  delay(500);
  sendStatusMessage();
}

void saveDoorState() {
  EEPROM.put(ADDR_DOOR_STATE, doorOpen);
  EEPROM.put(ADDR_SERVO_CLOSED_ANGLE, servoClosedAngle);
  EEPROM.put(ADDR_SERVO_OPEN_ANGLE, servoOpenAngle);
  EEPROM.put(ADDR_INIT_FLAG, 0xAA);
  EEPROM.commit();
}

void loadDoorState() {
  uint8_t initFlag;
  EEPROM.get(ADDR_INIT_FLAG, initFlag);
  
  if (initFlag == 0xAA) {
    EEPROM.get(ADDR_DOOR_STATE, doorOpen);
    EEPROM.get(ADDR_SERVO_CLOSED_ANGLE, servoClosedAngle);
    EEPROM.get(ADDR_SERVO_OPEN_ANGLE, servoOpenAngle);
    
    if (servoClosedAngle < 0 || servoClosedAngle > 180) servoClosedAngle = DEFAULT_CLOSED_ANGLE;
    if (servoOpenAngle < 0 || servoOpenAngle > 180) servoOpenAngle = DEFAULT_OPEN_ANGLE;
    
    servoAngle = doorOpen ? servoOpenAngle : servoClosedAngle;
    Serial.println("STATE LOADED");
  } else {
    doorOpen = false;
    servoClosedAngle = DEFAULT_CLOSED_ANGLE;
    servoOpenAngle = DEFAULT_OPEN_ANGLE;
    servoAngle = servoClosedAngle;
    Serial.println("DEFAULT STATE");
    saveDoorState();
  }
}

void loop() {
  if (millis() - lastStatusSent > 20000) {
    sendStatusMessage();
  }
  
  static unsigned long lastAlive = 0;
  if (millis() - lastAlive > 45000 && millis() - lastStatusSent > 10000) {
    sendAliveMessage();
    lastAlive = millis();
  }
  
  if (millis() - lastConnectionCheck > 5000) {
    if (millis() - lastHeartbeat > 120000) {
      Serial.println("RESTART");
      delay(1000);
      ESP.restart();
    }
    lastConnectionCheck = millis();
  }
  
  yield();
  delay(50);
}