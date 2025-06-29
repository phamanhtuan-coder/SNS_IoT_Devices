#define FIRMWARE_VERSION "1.0.0"
#define MASTER_ID "ESP_MASTER_GARDEN_001"

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ArduinoJson.h>

// ===== ESP MASTER GARDEN =====
// Nhận data từ Arduino Mega qua UART (Mega Serial3)
// Gửi data tới ESP07 Garden qua ESP-NOW

// ===== ESP07 GARDEN CONFIGURATION =====
String ESP07_GARDEN_SERIAL = "ESP07_GARDEN_001";
uint8_t esp07GardenMAC[6] = {0x84, 0x0D, 0x8E, 0xA4, 0x91, 0xFF}; // Thay bằng MAC thực của ESP07

// ===== ESP-NOW MESSAGE STRUCTURE =====
struct GardenMessage {
  char messageType[16];    // "sensor_data", "command", "status"
  char deviceSerial[32];   // ESP07 serial
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  char currentTime[16];
  char command[32];        // pump_on, pump_off, etc.
  bool success;
  unsigned long timestamp;
};

static GardenMessage sendBuffer;
static GardenMessage receiveBuffer;

// ===== GARDEN DATA STORAGE =====
struct GardenData {
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  String currentTime;
  unsigned long lastUpdated;
};

GardenData currentGardenData;

// ===== CONNECTION STATUS =====
bool megaConnected = false;
bool esp07Connected = false;
unsigned long lastMegaMessage = 0;
unsigned long lastESP07Message = 0;

// ===== STATISTICS =====
unsigned long messagesSent = 0;
unsigned long messagesReceived = 0;
unsigned long dataForwarded = 0;

// ✅ SAFE STRING COPY HELPER
void safeStringCopy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
  }
}

void setup() {
  Serial.begin(115200);  // Communication with Arduino Mega
  delay(2000);
  
  Serial.println("\n=== ESP MASTER GARDEN v1.0.0 ===");
  Serial.println("Master ID: " + String(MASTER_ID));
  Serial.println("Target ESP07: " + ESP07_GARDEN_SERIAL);
  Serial.println("My MAC: " + WiFi.macAddress());
  
  // Initialize garden data
  initializeGardenData();
  
  // Setup ESP-NOW
  setupESPNow();
  
  Serial.println("[INIT] ✓ ESP Master Garden Ready");
  Serial.println("Waiting for data from Mega Hub...\n");
}

void initializeGardenData() {
  currentGardenData.temperature = 0;
  currentGardenData.humidity = 0;
  currentGardenData.soilMoisture = 0;
  currentGardenData.lightLevel = 0;
  currentGardenData.rainDetected = false;
  currentGardenData.pumpRunning = false;
  currentGardenData.currentTime = "00:00:00";
  currentGardenData.lastUpdated = 0;
  
  Serial.println("[INIT] ✓ Garden data structure initialized");
}

void setupESPNow() {
  Serial.println("[ESP-NOW] Initializing...");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(1);
  wifi_set_sleep_type(NONE_SLEEP_T);
  
  int initResult = esp_now_init();
  if (initResult != 0) {
    Serial.println("[ESP-NOW] Init FAILED: " + String(initResult));
    delay(5000);
    ESP.restart();
  }
  
  Serial.println("[ESP-NOW] Init OK");
  
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  // Add ESP07 Garden as peer
  Serial.print("[ESP-NOW] Adding ESP07 Garden peer: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", esp07GardenMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  int result = esp_now_add_peer(esp07GardenMAC, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  if (result != 0) {
    Serial.println("[ESP-NOW] ✗ ESP07 peer add failed: " + String(result));
    delay(100);
    result = esp_now_add_peer(esp07GardenMAC, ESP_NOW_ROLE_SLAVE, 0, NULL, 0);
    if (result == 0) {
      Serial.println("[ESP-NOW] ✓ ESP07 peer added (retry)");
    }
  } else {
    Serial.println("[ESP-NOW] ✓ ESP07 peer added");
  }
  
  Serial.println("[ESP-NOW] Setup complete");
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  messagesSent++;
  if (sendStatus == 0) {
    Serial.println("[ESP-NOW] ✓ TX #" + String(messagesSent) + " to ESP07");
  } else {
    Serial.println("[ESP-NOW] ✗ TX #" + String(messagesSent) + " failed: " + String(sendStatus));
  }
}

void onDataReceived(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  messagesReceived++;
  
  if (memcmp(mac_addr, esp07GardenMAC, 6) != 0 || len != sizeof(GardenMessage)) {
    Serial.println("[ESP-NOW] RX #" + String(messagesReceived) + " ✗ Invalid");
    return;
  }
  
  memcpy(&receiveBuffer, incomingData, len);
  
  esp07Connected = true;
  lastESP07Message = millis();
  
  String msgType = String(receiveBuffer.messageType);
  Serial.println("[ESP-NOW] RX #" + String(messagesReceived) + " from ESP07: " + msgType);
  
  if (msgType == "status") {
    Serial.println("[ESP07] Status: " + String(receiveBuffer.success ? "OK" : "ERROR"));
    
    // Forward status to Mega
    String response = "RESP:{";
    response += "\"type\":\"garden_status\",";
    response += "\"esp07_online\":true,";
    response += "\"display_active\":" + String(receiveBuffer.success ? "true" : "false") + ",";
    response += "\"timestamp\":" + String(receiveBuffer.timestamp);
    response += "}";
    
    Serial.println(response);
    
  } else if (msgType == "heartbeat") {
    Serial.println("[ESP07] Heartbeat received");
  }
}

void loop() {
  // ===== HANDLE MEGA COMMUNICATION =====
  if (Serial.available()) {
    String megaMessage = Serial.readStringUntil('\n');
    handleMegaMessage(megaMessage);
  }
  
  // ===== PERIODIC TASKS =====
  static unsigned long lastHealthCheck = 0;
  if (millis() - lastHealthCheck > 30000) {  // Every 30 seconds
    checkConnectionHealth();
    lastHealthCheck = millis();
  }
  
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 120000) {  // Every 2 minutes
    printMasterStatus();
    lastStatusPrint = millis();
  }
  
  // ===== HEARTBEAT TO ESP07 =====
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 45000) {  // Every 45 seconds
    sendHeartbeatToESP07();
    lastHeartbeat = millis();
  }
  
  yield();
  delay(10);
}

void handleMegaMessage(String message) {
  message.trim();
  if (message.length() == 0) return;
  
  megaConnected = true;
  lastMegaMessage = millis();
  
  Serial.println("[MEGA→MASTER] " + message);
  
  if (message.startsWith("GARDEN_DATA:")) {
    handleGardenData(message);
  } else if (message.startsWith("PUMP_STATUS:")) {
    handlePumpStatus(message);
  } else {
    Serial.println("[MEGA] Unknown message: " + message);
  }
}

void handleGardenData(String dataMessage) {
  // Parse: GARDEN_DATA:temp,humidity,soil,light,rain,pump,time
  String data = dataMessage.substring(12);  // Remove "GARDEN_DATA:"
  
  // Split by comma
  int commaCount = 0;
  String values[7];
  int startIndex = 0;
  
  for (int i = 0; i <= data.length() && commaCount < 7; i++) {
    if (i == data.length() || data.charAt(i) == ',') {
      values[commaCount] = data.substring(startIndex, i);
      startIndex = i + 1;
      commaCount++;
    }
  }
  
  if (commaCount >= 6) {
    // Update current garden data
    currentGardenData.temperature = values[0].toFloat();
    currentGardenData.humidity = values[1].toFloat();
    currentGardenData.soilMoisture = values[2].toInt();
    currentGardenData.lightLevel = values[3].toInt();
    currentGardenData.rainDetected = (values[4].toInt() == 1);
    currentGardenData.pumpRunning = (values[5].toInt() == 1);
    currentGardenData.currentTime = values[6];
    currentGardenData.lastUpdated = millis();
    
    Serial.println("[DATA] Updated: " + String(currentGardenData.temperature) + "°C, " +
                   String(currentGardenData.humidity) + "%, " +
                   "Soil:" + String(currentGardenData.soilMoisture) + "%, " +
                   "Light:" + String(currentGardenData.lightLevel) + ", " +
                   "Pump:" + String(currentGardenData.pumpRunning ? "ON" : "OFF"));
    
    // Send to ESP07
    sendGardenDataToESP07();
    dataForwarded++;
  } else {
    Serial.println("[DATA] ✗ Invalid format: " + dataMessage);
  }
}

void handlePumpStatus(String statusMessage) {
  // Parse: PUMP_STATUS:1 or PUMP_STATUS:0
  String status = statusMessage.substring(12);  // Remove "PUMP_STATUS:"
  
  currentGardenData.pumpRunning = (status.toInt() == 1);
  
  Serial.println("[PUMP] Status updated: " + String(currentGardenData.pumpRunning ? "RUNNING" : "STOPPED"));
  
  // Send pump command to ESP07
  sendPumpCommandToESP07();
}

void sendGardenDataToESP07() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "sensor_data", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.deviceSerial, ESP07_GARDEN_SERIAL.c_str(), sizeof(sendBuffer.deviceSerial));
  
  sendBuffer.temperature = currentGardenData.temperature;
  sendBuffer.humidity = currentGardenData.humidity;
  sendBuffer.soilMoisture = currentGardenData.soilMoisture;
  sendBuffer.lightLevel = currentGardenData.lightLevel;
  sendBuffer.rainDetected = currentGardenData.rainDetected;
  sendBuffer.pumpRunning = currentGardenData.pumpRunning;
  
  safeStringCopy(sendBuffer.currentTime, currentGardenData.currentTime.c_str(), sizeof(sendBuffer.currentTime));
  safeStringCopy(sendBuffer.command, "display_update", sizeof(sendBuffer.command));
  
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  delay(10);
  int result = esp_now_send(esp07GardenMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[ESP-NOW] ✓ Garden data sent to ESP07");
  } else {
    Serial.println("[ESP-NOW] ✗ Data send failed: " + String(result));
  }
}

void sendPumpCommandToESP07() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "command", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.deviceSerial, ESP07_GARDEN_SERIAL.c_str(), sizeof(sendBuffer.deviceSerial));
  
  String cmd = currentGardenData.pumpRunning ? "pump_on" : "pump_off";
  safeStringCopy(sendBuffer.command, cmd.c_str(), sizeof(sendBuffer.command));
  
  sendBuffer.pumpRunning = currentGardenData.pumpRunning;
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  delay(10);
  int result = esp_now_send(esp07GardenMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[ESP-NOW] ✓ Pump command sent: " + cmd);
  } else {
    Serial.println("[ESP-NOW] ✗ Command send failed: " + String(result));
  }
}

void sendHeartbeatToESP07() {
  memset(&sendBuffer, 0, sizeof(sendBuffer));
  
  safeStringCopy(sendBuffer.messageType, "heartbeat", sizeof(sendBuffer.messageType));
  safeStringCopy(sendBuffer.deviceSerial, ESP07_GARDEN_SERIAL.c_str(), sizeof(sendBuffer.deviceSerial));
  safeStringCopy(sendBuffer.command, "ping", sizeof(sendBuffer.command));
  
  sendBuffer.success = true;
  sendBuffer.timestamp = millis();
  
  delay(20);
  int result = esp_now_send(esp07GardenMAC, (uint8_t*)&sendBuffer, sizeof(sendBuffer));
  
  if (result == 0) {
    Serial.println("[HEARTBEAT] ✓ Sent to ESP07");
  } else {
    Serial.println("[HEARTBEAT] ✗ Failed: " + String(result));
  }
}

void checkConnectionHealth() {
  // Check Mega connection
  if (megaConnected && (millis() - lastMegaMessage > 120000)) {
    megaConnected = false;
    Serial.println("[HEALTH] ✗ Mega Hub timeout");
  }
  
  // Check ESP07 connection
  if (esp07Connected && (millis() - lastESP07Message > 180000)) {
    esp07Connected = false;
    Serial.println("[HEALTH] ✗ ESP07 Garden timeout");
  }
}

void printMasterStatus() {
  Serial.println("\n======= ESP MASTER GARDEN STATUS =======");
  Serial.println("Master ID: " + String(MASTER_ID));
  Serial.println("Uptime: " + String(millis() / 1000) + " seconds");
  Serial.println("Messages TX/RX: " + String(messagesSent) + "/" + String(messagesReceived));
  Serial.println("Data Forwarded: " + String(dataForwarded));
  
  Serial.println("\n--- Connections ---");
  Serial.println("Mega Hub: " + String(megaConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("ESP07 Garden: " + String(esp07Connected ? "CONNECTED" : "DISCONNECTED"));
  
  Serial.println("\n--- Current Garden Data ---");
  Serial.println("Temperature: " + String(currentGardenData.temperature) + "°C");
  Serial.println("Humidity: " + String(currentGardenData.humidity) + "%");
  Serial.println("Soil Moisture: " + String(currentGardenData.soilMoisture) + "%");
  Serial.println("Light Level: " + String(currentGardenData.lightLevel) + " lux");
  Serial.println("Rain: " + String(currentGardenData.rainDetected ? "DETECTED" : "NONE"));
  Serial.println("Pump: " + String(currentGardenData.pumpRunning ? "RUNNING" : "STOPPED"));
  Serial.println("Time: " + currentGardenData.currentTime);
  Serial.println("Last Update: " + String((millis() - currentGardenData.lastUpdated) / 1000) + "s ago");
  
  Serial.println("\nFree Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("=========================================\n");
}