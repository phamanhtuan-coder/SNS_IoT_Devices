// ========================================
// ESP8266 SLIDING DOOR - PIR CONFIGURABLE VERSION v5.2.0
// Enhanced PIR sensitivity and control options
// ========================================

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define FIRMWARE_VERSION "5.2.0"
#define DEVICE_TYPE "ESP8266_SLIDING_DOOR"
#define DOOR_TYPE "SLIDING"
#define DOOR_ID 10

// ✅ DEVICE CONFIGURATION
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVSQGM7E9S9D9A";
String WIFI_SSID = "Anh Tuan";
String WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;

// ✅ HARDWARE PINS (NodeMCU v3)
#define MOTOR_PIN1 D1        // GPIO5, IN1 của L298N
#define MOTOR_PIN2 D2        // GPIO4, IN2 của L298N  
#define ENA_PIN D6           // GPIO12, ENA để điều khiển tốc độ
#define PIR1_PIN D4          // GPIO2, PIR sensor 1
#define PIR2_PIN D5          // GPIO14, PIR sensor 2
#define BUTTON_PIN D3        // GPIO0, Manual button
#define CONFIG_BUTTON_PIN D0 // GPIO16, Config button

// ✅ EEPROM ADDRESSES
#define EEPROM_SIZE 512
#define ADDR_DOOR_STATE 0
#define ADDR_MOTOR_SPEED 4
#define ADDR_OPEN_DURATION 8
#define ADDR_WAIT_TIME 12
#define ADDR_AUTO_MODE 16
#define ADDR_INIT_FLAG 20
#define ADDR_PIR1_ENABLED 24
#define ADDR_PIR2_ENABLED 28
#define ADDR_PIR1_SENSITIVITY 32
#define ADDR_PIR2_SENSITIVITY 36
#define ADDR_PIR_DEBOUNCE 40
#define ADDR_MOTION_TIMEOUT 44
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 164
#define WIFI_SERIAL_ADDR 228
#define WIFI_CONFIG_FLAG_ADDR 292

// ✅ WIFI CONFIG
WiFiUDP udp;
ESP8266WebServer webServer(80);
const int UDP_PORT = 12345;
bool configMode = false;
unsigned long configModeStart = 0;
const unsigned long CONFIG_TIMEOUT = 300000;

// ✅ ENHANCED PIR CONFIGURATION
struct PIRConfig {
  bool pir1Enabled;               // PIR1 sensor enabled
  bool pir2Enabled;               // PIR2 sensor enabled
  int pir1Sensitivity;            // PIR1 sensitivity (1-10, 10=most sensitive)
  int pir2Sensitivity;            // PIR2 sensitivity (1-10, 10=most sensitive)
  unsigned long pirDebounceTime;  // Debounce time in ms (100-2000ms)
  unsigned long motionTimeout;    // Motion detection timeout (500-5000ms)
  bool requireBothSensors;        // Require both sensors for activation
  bool smartDetection;            // Smart motion detection
};

// ✅ ENHANCED DOOR CONFIGURATION
struct SlidingDoorConfig {
  int motorSpeed;                      // PWM speed (0-255)
  unsigned long openDuration;         // Time to open (ms)
  unsigned long closeDuration;        // Time to close (ms)
  unsigned long waitBeforeClose;      // Wait time before auto close (ms)
  bool autoMode;                      // Auto operation with PIR
  bool reversed;                      // Reverse motor direction
  PIRConfig pirConfig;               // PIR configuration
};

SlidingDoorConfig doorConfig = {
  200,    // motorSpeed
  3000,   // openDuration
  3000,   // closeDuration
  5000,   // waitBeforeClose
  true,   // autoMode
  false,  // reversed
  {
    true,   // pir1Enabled
    true,   // pir2Enabled
    7,      // pir1Sensitivity (1-10)
    7,      // pir2Sensitivity (1-10)
    200,    // pirDebounceTime (ms)
    1000,   // motionTimeout (ms)
    false,  // requireBothSensors
    true    // smartDetection
  }
};

// ✅ PIR STATE TRACKING
struct PIRState {
  bool pir1CurrentState;
  bool pir2CurrentState;
  bool pir1LastState;
  bool pir2LastState;
  unsigned long pir1LastTrigger;
  unsigned long pir2LastTrigger;
  unsigned long pir1LastDebounce;
  unsigned long pir2LastDebounce;
  int pir1TriggerCount;
  int pir2TriggerCount;
  unsigned long motionStartTime;
  bool motionActive;
};

PIRState pirState = {false, false, false, false, 0, 0, 0, 0, 0, 0, 0, false};

// ✅ DOOR STATE
enum DoorState {
  DOOR_CLOSED = 0,
  DOOR_OPENING = 1, 
  DOOR_OPEN = 2,
  DOOR_CLOSING = 3
};

DoorState doorState = DOOR_CLOSED;
bool isDoorOpen = false;
bool isMoving = false;
unsigned long lastMotionTime = 0;
unsigned long motorStartTime = 0;
unsigned long movementTimeout = 0;

// ✅ WEBSOCKET STATUS
WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;

// ✅ BUTTON HANDLING
bool lastButtonState = HIGH;
unsigned long lastButtonTime = 0;
const unsigned long buttonDebounce = 50;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP8266 SLIDING DOOR v5.2.0 (PIR CONFIGURABLE) ===");
  Serial.println("Device: " + DEVICE_SERIAL);
  Serial.println("Type: " + String(DEVICE_TYPE));
  Serial.println("Features: Configurable PIR Sensitivity & Control");
  
  EEPROM.begin(EEPROM_SIZE);
  initializeHardware();
  
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  delay(100);
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    Serial.println("[CONFIG] Config mode activated");
    startConfigMode();
  } else {
    loadDoorConfig();
    loadDoorState();
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      setupWebSocket();
    }
  }
  
  printPIRConfig();
  Serial.println("✓ Sliding Door Ready - Auto Mode: " + String(doorConfig.autoMode ? "ON" : "OFF"));
  Serial.println("=================================================\n");
}

void initializeHardware() {
  // Motor pins
  pinMode(MOTOR_PIN1, OUTPUT);
  pinMode(MOTOR_PIN2, OUTPUT);
  pinMode(ENA_PIN, OUTPUT);
  
  // Sensor pins
  pinMode(PIR1_PIN, INPUT);
  pinMode(PIR2_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Stop motor initially
  stopMotor();
  
  Serial.println("[INIT] ✓ Hardware initialized");
}

void printPIRConfig() {
  Serial.println("[PIR CONFIG]");
  Serial.println("  PIR1: " + String(doorConfig.pirConfig.pir1Enabled ? "ENABLED" : "DISABLED") + 
                 " | Sensitivity: " + String(doorConfig.pirConfig.pir1Sensitivity) + "/10");
  Serial.println("  PIR2: " + String(doorConfig.pirConfig.pir2Enabled ? "ENABLED" : "DISABLED") + 
                 " | Sensitivity: " + String(doorConfig.pirConfig.pir2Sensitivity) + "/10");
  Serial.println("  Debounce: " + String(doorConfig.pirConfig.pirDebounceTime) + "ms");
  Serial.println("  Motion Timeout: " + String(doorConfig.pirConfig.motionTimeout) + "ms");
  Serial.println("  Require Both: " + String(doorConfig.pirConfig.requireBothSensors ? "YES" : "NO"));
  Serial.println("  Smart Detection: " + String(doorConfig.pirConfig.smartDetection ? "YES" : "NO"));
}

// ===== WIFI CONFIG FUNCTIONS =====
void startConfigMode() {
  configMode = true;
  configModeStart = millis();
  
  String apName = "ESP_SLIDING_" + String(DOOR_ID);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str(), "12345678");
  
  IPAddress apIP = WiFi.softAPIP();
  Serial.println("[CONFIG] AP: " + apName + " | IP: " + apIP.toString());
  
  udp.begin(UDP_PORT);
  setupWebServer();
  webServer.begin();
}

void setupWebServer() {
  webServer.on("/", handleConfigPage);
  webServer.on("/save", HTTP_POST, handleSaveConfig);
  webServer.on("/status", HTTP_GET, handleStatus);
  webServer.on("/pir", HTTP_GET, handlePIRStatus);
  webServer.on("/pir/config", HTTP_POST, handlePIRConfig);
  webServer.onNotFound(handleNotFound);
}

void handleConfigPage() {
  String html = R"(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>Sliding Door + PIR Config</title><style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px;color:#333}.container{max-width:600px;margin:0 auto;background:white;border-radius:15px;box-shadow:0 20px 40px rgba(0,0,0,0.1);overflow:hidden;animation:slideUp 0.5s ease}@keyframes slideUp{from{opacity:0;transform:translateY(30px)}}.header{background:linear-gradient(135deg,#4CAF50,#45a049);color:white;padding:25px;text-align:center}.header h1{font-size:24px;margin-bottom:8px}.header p{opacity:0.9;font-size:14px}.form{padding:30px}.section{margin-bottom:25px;padding:20px;border:1px solid #e1e5e9;border-radius:8px}.section h3{margin-bottom:15px;color:#333;border-bottom:2px solid #4CAF50;padding-bottom:5px}.field{margin-bottom:15px}.field label{display:block;margin-bottom:8px;font-weight:600;color:#555}.field input,.field select{width:100%;padding:12px 15px;border:2px solid #e1e5e9;border-radius:8px;font-size:16px;transition:all 0.3s}.field input:focus,.field select:focus{outline:none;border-color:#4CAF50;box-shadow:0 0 0 3px rgba(76,175,80,0.1)}.field.inline{display:flex;align-items:center}.field.inline input[type="checkbox"]{width:auto;margin-right:10px}.btn{width:100%;padding:15px;background:linear-gradient(135deg,#4CAF50,#45a049);color:white;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;transition:all 0.3s;margin-top:10px}.btn:hover{transform:translateY(-2px);box-shadow:0 5px 15px rgba(76,175,80,0.3)}.btn.secondary{background:linear-gradient(135deg,#2196F3,#1976D2)}.status{padding:15px;margin:15px 0;border-radius:8px;text-align:center;font-weight:600}.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}.footer{background:#f8f9fa;padding:20px;text-align:center;color:#666;font-size:12px;border-top:1px solid #e9ecef}</style></head><body><div class="container"><div class="header"><h1>🚪 Sliding Door + PIR Config</h1><p>ESP8266 WiFi & PIR Configuration</p></div><div class="form"><div id="status"></div><div class="section"><h3>📶 WiFi Configuration</h3><form id="wifiForm"><div class="field"><label for="ssid">🌐 WiFi Network Name (SSID)</label><input type="text" id="ssid" name="ssid" required maxlength="31" placeholder="Enter your WiFi name"></div><div class="field"><label for="password">🔐 WiFi Password</label><input type="password" id="password" name="password" required maxlength="31" placeholder="Enter your WiFi password"></div><div class="field"><label for="serial">🏷️ Device Serial (Optional)</label><input type="text" id="serial" name="serial" maxlength="31" placeholder="Leave empty to keep current"></div><button type="submit" class="btn">💾 Save WiFi Configuration</button></form></div><div class="section"><h3>🎯 PIR Sensor Configuration</h3><form id="pirForm"><div class="field inline"><input type="checkbox" id="pir1_enabled" name="pir1_enabled" checked><label for="pir1_enabled">Enable PIR Sensor 1</label></div><div class="field"><label for="pir1_sensitivity">PIR1 Sensitivity (1-10)</label><input type="range" id="pir1_sensitivity" name="pir1_sensitivity" min="1" max="10" value="7"><span id="pir1_value">7</span></div><div class="field inline"><input type="checkbox" id="pir2_enabled" name="pir2_enabled" checked><label for="pir2_enabled">Enable PIR Sensor 2</label></div><div class="field"><label for="pir2_sensitivity">PIR2 Sensitivity (1-10)</label><input type="range" id="pir2_sensitivity" name="pir2_sensitivity" min="1" max="10" value="7"><span id="pir2_value">7</span></div><div class="field"><label for="debounce_time">Debounce Time (100-2000ms)</label><input type="number" id="debounce_time" name="debounce_time" min="100" max="2000" value="200"></div><div class="field"><label for="motion_timeout">Motion Timeout (500-5000ms)</label><input type="number" id="motion_timeout" name="motion_timeout" min="500" max="5000" value="1000"></div><div class="field inline"><input type="checkbox" id="require_both" name="require_both"><label for="require_both">Require Both Sensors</label></div><div class="field inline"><input type="checkbox" id="smart_detection" name="smart_detection" checked><label for="smart_detection">Smart Detection</label></div><button type="submit" class="btn secondary">🎯 Save PIR Configuration</button></form></div></div><div class="footer">ESP8266 Sliding Door v5.2.0 | PIR Configurable</div></div><script>document.getElementById('pir1_sensitivity').addEventListener('input', function(e) {document.getElementById('pir1_value').textContent = e.target.value;});document.getElementById('pir2_sensitivity').addEventListener('input', function(e) {document.getElementById('pir2_value').textContent = e.target.value;});document.getElementById('wifiForm').addEventListener('submit', async function(e) {e.preventDefault();const statusDiv = document.getElementById('status');const formData = new FormData(this);const submitBtn = this.querySelector('button[type="submit"]');const ssid = formData.get('ssid').trim();const password = formData.get('password').trim();if (!ssid || !password) {statusDiv.innerHTML = '<div class="status error">❌ SSID and Password are required</div>';return;}submitBtn.disabled = true;submitBtn.textContent = '⏳ Saving...';statusDiv.innerHTML = '<div class="status">⏳ Saving WiFi configuration...</div>';try {const response = await fetch('/save', {method: 'POST',body: formData});const result = await response.json();if (result.success) {statusDiv.innerHTML = '<div class="status success">✅ ' + result.message + '</div>';} else {statusDiv.innerHTML = '<div class="status error">❌ ' + result.message + '</div>';submitBtn.disabled = false;submitBtn.textContent = '💾 Save WiFi Configuration';}} catch (error) {statusDiv.innerHTML = '<div class="status error">❌ Connection error: ' + error.message + '</div>';submitBtn.disabled = false;submitBtn.textContent = '💾 Save WiFi Configuration';}});document.getElementById('pirForm').addEventListener('submit', async function(e) {e.preventDefault();const statusDiv = document.getElementById('status');const formData = new FormData(this);const submitBtn = this.querySelector('button[type="submit"]');submitBtn.disabled = true;submitBtn.textContent = '⏳ Saving PIR...';statusDiv.innerHTML = '<div class="status">⏳ Saving PIR configuration...</div>';try {const response = await fetch('/pir/config', {method: 'POST',body: formData});const result = await response.json();if (result.success) {statusDiv.innerHTML = '<div class="status success">✅ PIR Configuration saved!</div>';} else {statusDiv.innerHTML = '<div class="status error">❌ ' + result.message + '</div>';}submitBtn.disabled = false;submitBtn.textContent = '🎯 Save PIR Configuration';} catch (error) {statusDiv.innerHTML = '<div class="status error">❌ PIR config error: ' + error.message + '</div>';submitBtn.disabled = false;submitBtn.textContent = '🎯 Save PIR Configuration';}});</script></body></html>)";
  
  webServer.send(200, "text/html", html);
}

void handleSaveConfig() {
  String response;
  bool success = false;
  
  if (webServer.hasArg("ssid") && webServer.hasArg("password")) {
    String newSSID = webServer.arg("ssid");
    String newPassword = webServer.arg("password");
    String newSerial = webServer.arg("serial");
    
    if (newSSID.length() > 0 && newSSID.length() <= 31 && 
        newPassword.length() > 0 && newPassword.length() <= 31) {
      
      if (newSerial.length() > 0 && newSerial.length() <= 31) {
        DEVICE_SERIAL = newSerial;
      }
      
      saveWiFiConfig(newSSID, newPassword, newSerial);
      
      success = true;
      response = "{\"success\":true,\"message\":\"WiFi Configuration saved! Device will restart in 3 seconds.\"}";
      
      Serial.println("[CONFIG] ✓ WiFi config saved");
      
    } else {
      response = "{\"success\":false,\"message\":\"Invalid SSID or password length\"}";
    }
  } else {
    response = "{\"success\":false,\"message\":\"Missing SSID or password\"}";
  }
  
  webServer.send(200, "application/json", response);
  
  if (success) {
    delay(3000);
    ESP.restart();
  }
}

void handlePIRConfig() {
  bool success = false;
  String response;
  
  // Parse PIR configuration
  doorConfig.pirConfig.pir1Enabled = webServer.hasArg("pir1_enabled");
  doorConfig.pirConfig.pir2Enabled = webServer.hasArg("pir2_enabled");
  doorConfig.pirConfig.requireBothSensors = webServer.hasArg("require_both");
  doorConfig.pirConfig.smartDetection = webServer.hasArg("smart_detection");
  
  if (webServer.hasArg("pir1_sensitivity")) {
    doorConfig.pirConfig.pir1Sensitivity = webServer.arg("pir1_sensitivity").toInt();
  }
  if (webServer.hasArg("pir2_sensitivity")) {
    doorConfig.pirConfig.pir2Sensitivity = webServer.arg("pir2_sensitivity").toInt();
  }
  if (webServer.hasArg("debounce_time")) {
    doorConfig.pirConfig.pirDebounceTime = webServer.arg("debounce_time").toInt();
  }
  if (webServer.hasArg("motion_timeout")) {
    doorConfig.pirConfig.motionTimeout = webServer.arg("motion_timeout").toInt();
  }
  
  // Validate ranges
  if (doorConfig.pirConfig.pir1Sensitivity < 1 || doorConfig.pirConfig.pir1Sensitivity > 10) {
    doorConfig.pirConfig.pir1Sensitivity = 7;
  }
  if (doorConfig.pirConfig.pir2Sensitivity < 1 || doorConfig.pirConfig.pir2Sensitivity > 10) {
    doorConfig.pirConfig.pir2Sensitivity = 7;
  }
  if (doorConfig.pirConfig.pirDebounceTime < 100 || doorConfig.pirConfig.pirDebounceTime > 2000) {
    doorConfig.pirConfig.pirDebounceTime = 200;
  }
  if (doorConfig.pirConfig.motionTimeout < 500 || doorConfig.pirConfig.motionTimeout > 5000) {
    doorConfig.pirConfig.motionTimeout = 1000;
  }
  
  saveDoorConfig();
  success = true;
  response = "{\"success\":true,\"message\":\"PIR configuration saved successfully!\"}";
  
  Serial.println("[PIR] Configuration updated:");
  printPIRConfig();
  
  webServer.send(200, "application/json", response);
}

void handlePIRStatus() {
  StaticJsonDocument<512> doc;
  doc["pir1_enabled"] = doorConfig.pirConfig.pir1Enabled;
  doc["pir2_enabled"] = doorConfig.pirConfig.pir2Enabled;
  doc["pir1_sensitivity"] = doorConfig.pirConfig.pir1Sensitivity;
  doc["pir2_sensitivity"] = doorConfig.pirConfig.pir2Sensitivity;
  doc["debounce_time"] = doorConfig.pirConfig.pirDebounceTime;
  doc["motion_timeout"] = doorConfig.pirConfig.motionTimeout;
  doc["require_both"] = doorConfig.pirConfig.requireBothSensors;
  doc["smart_detection"] = doorConfig.pirConfig.smartDetection;
  doc["pir1_current"] = readPIR1();
  doc["pir2_current"] = readPIR2();
  doc["motion_active"] = pirState.motionActive;
  doc["auto_mode"] = doorConfig.autoMode;
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void handleStatus() {
  StaticJsonDocument<256> doc;
  doc["device_type"] = DEVICE_TYPE;
  doc["door_type"] = DOOR_TYPE;
  doc["device_serial"] = DEVICE_SERIAL;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["door_state"] = getStateString();
  doc["auto_mode"] = doorConfig.autoMode;
  doc["pir1_enabled"] = doorConfig.pirConfig.pir1Enabled;
  doc["pir2_enabled"] = doorConfig.pirConfig.pir2Enabled;
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void handleNotFound() {
  webServer.send(404, "text/plain", "Not found");
}

void handleConfigMode() {
  if (!configMode) return;
  
  if (millis() - configModeStart > CONFIG_TIMEOUT) {
    ESP.restart();
  }
  
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 500) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastBlink = millis();
  }
  
  webServer.handleClient();
  handleUDPConfig();
}

void handleUDPConfig() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packet[256];
    int len = udp.read(packet, 255);
    if (len > 0) {
      packet[len] = 0;
      String message = String(packet);
      
      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, message) == DeserializationError::Ok) {
        String newSSID = doc["ssid"].as<String>();
        String newPassword = doc["password"].as<String>();
        String newSerial = doc["serial"].as<String>();
        
        if (newSSID.length() > 0 && newSSID.length() <= 31 && 
            newPassword.length() > 0 && newPassword.length() <= 31) {
          
          if (newSerial.length() > 0) DEVICE_SERIAL = newSerial;
          
          saveWiFiConfig(newSSID, newPassword, newSerial);
          
          String response = "{\"success\":true,\"message\":\"Config saved, restarting...\"}";
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(response.c_str());
          udp.endPacket();
          
          delay(2000);
          ESP.restart();
        }
      }
    }
  }
}

void saveWiFiConfig(String ssid, String password, String serial) {
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, i < ssid.length() ? ssid[i] : 0);
    EEPROM.write(WIFI_PASS_ADDR + i, i < password.length() ? password[i] : 0);
    EEPROM.write(WIFI_SERIAL_ADDR + i, i < serial.length() ? serial[i] : 0);
  }
  EEPROM.write(WIFI_CONFIG_FLAG_ADDR, 0xAB);
  EEPROM.commit();
}

bool loadWiFiConfig() {
  if (EEPROM.read(WIFI_CONFIG_FLAG_ADDR) != 0xAB) return false;
  
  char ssid[33] = {0}, password[33] = {0}, serial[33] = {0};
  
  for (int i = 0; i < 32; i++) {
    ssid[i] = EEPROM.read(WIFI_SSID_ADDR + i);
    password[i] = EEPROM.read(WIFI_PASS_ADDR + i);
    serial[i] = EEPROM.read(WIFI_SERIAL_ADDR + i);
  }
  
  WIFI_SSID = String(ssid);
  WIFI_PASSWORD = String(password);
  if (strlen(serial) > 0) DEVICE_SERIAL = String(serial);
  
  Serial.println("[CONFIG] ✓ WiFi config loaded");
  return true;
}

void setupWiFi() {
  if (!loadWiFiConfig()) {
    Serial.println("[WiFi] Using defaults");
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  
  Serial.print("[WiFi] Connecting to: " + WIFI_SSID);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✓ CONNECTED");
    Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    Serial.println(" ✗ FAILED - Starting config mode");
    startConfigMode();
  }
}

void setupWebSocket() {
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                DEVICE_SERIAL + "&isIoTDevice=true";
  
  Serial.println("[WS] Connecting to " + WEBSOCKET_HOST);
  
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(25000, 5000, 2);
  
  String userAgent = "ESP8266-Sliding-Door/5.2.0";
  webSocket.setExtraHeaders(("User-Agent: " + userAgent).c_str());
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] ✗ DISCONNECTED");
      socketConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WS] ✓ CONNECTED - " + DEVICE_SERIAL);
      socketConnected = true;
      lastPingResponse = millis();
      sendDeviceOnline();
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(String((char*)payload));
      break;
      
    case WStype_PONG:
      lastPingResponse = millis();
      break;
  }
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  
  if (type == '2') {
    webSocket.sendTXT("3");
    lastPingResponse = millis();
  } else if (type == '3') {
    lastPingResponse = millis();
  } else if (type == '4') {
    handleSocketIOMessage(message.substring(1));
  }
}

void handleSocketIOMessage(String data) {
  if (data.length() < 1) return;
  
  if (data.charAt(0) == '2') {
    handleSocketIOEvent(data.substring(1));
  }
}

void handleSocketIOEvent(String eventData) {
  if (eventData.indexOf("command") != -1) {
    parseAndExecuteCommand(eventData);
  } else if (eventData.indexOf("status_request") != -1) {
    sendDoorStatus();
  } else if (eventData.indexOf("ping") != -1) {
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"device_serial\":\"" + DEVICE_SERIAL + "\"}]";
    webSocket.sendTXT(pongPayload);
    lastPingResponse = millis();
  }
}

void parseAndExecuteCommand(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx == -1 || endIdx == -1) return;
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, jsonString) != DeserializationError::Ok) return;
  
  String action = doc["action"].as<String>();
  String serialNumber = doc["serialNumber"].as<String>();
  
  if (serialNumber != DEVICE_SERIAL) return;
  
  Serial.println("[CMD] Processing: " + action);
  
  bool success = false;
  String result = "unknown";
  
  if (action == "open_door" || action == "OPN") {
    success = openDoor();
    result = success ? "opening" : "error";
  } else if (action == "close_door" || action == "CLS") {
    success = closeDoor();
    result = success ? "closing" : "error";
  } else if (action == "toggle_door" || action == "TGL") {
    success = toggleDoor();
    result = success ? (isDoorOpen ? "closing" : "opening") : "error";
  } else if (action == "toggle_pir" || action == "PIR") {
    doorConfig.autoMode = !doorConfig.autoMode;
    saveDoorConfig();
    success = true;
    result = doorConfig.autoMode ? "auto_enabled" : "auto_disabled";
  } else if (action == "configure_pir" || action == "PIR_CONFIG") {
    success = configurePIR(doc);
    result = success ? "pir_configured" : "pir_config_error";
  } else {
    result = "unknown_command";
  }
  
  sendCommandResponse(action, success, result);
}

bool configurePIR(DynamicJsonDocument &doc) {
  if (doc.containsKey("pir1_enabled")) {
    doorConfig.pirConfig.pir1Enabled = doc["pir1_enabled"].as<bool>();
  }
  if (doc.containsKey("pir2_enabled")) {
    doorConfig.pirConfig.pir2Enabled = doc["pir2_enabled"].as<bool>();
  }
  if (doc.containsKey("pir1_sensitivity")) {
    int sens = doc["pir1_sensitivity"].as<int>();
    if (sens >= 1 && sens <= 10) {
      doorConfig.pirConfig.pir1Sensitivity = sens;
    }
  }
  if (doc.containsKey("pir2_sensitivity")) {
    int sens = doc["pir2_sensitivity"].as<int>();
    if (sens >= 1 && sens <= 10) {
      doorConfig.pirConfig.pir2Sensitivity = sens;
    }
  }
  if (doc.containsKey("debounce_time")) {
    unsigned long debounce = doc["debounce_time"].as<unsigned long>();
    if (debounce >= 100 && debounce <= 2000) {
      doorConfig.pirConfig.pirDebounceTime = debounce;
    }
  }
  if (doc.containsKey("motion_timeout")) {
    unsigned long timeout = doc["motion_timeout"].as<unsigned long>();
    if (timeout >= 500 && timeout <= 5000) {
      doorConfig.pirConfig.motionTimeout = timeout;
    }
  }
  if (doc.containsKey("require_both")) {
    doorConfig.pirConfig.requireBothSensors = doc["require_both"].as<bool>();
  }
  if (doc.containsKey("smart_detection")) {
    doorConfig.pirConfig.smartDetection = doc["smart_detection"].as<bool>();
  }
  
  saveDoorConfig();
  Serial.println("[PIR] Configuration updated via websocket");
  printPIRConfig();
  return true;
}

// ===== DOOR CONTROL FUNCTIONS =====
bool openDoor() {
  if (isMoving) {
    Serial.println("[DOOR] Already moving");
    return false;
  }
  
  if (isDoorOpen) {
    Serial.println("[DOOR] Already open");
    return false;
  }
  
  Serial.println("[DOOR] Opening - Duration: " + String(doorConfig.openDuration) + "ms, Speed: " + String(doorConfig.motorSpeed));
  
  isMoving = true;
  doorState = DOOR_OPENING;
  motorStartTime = millis();
  movementTimeout = motorStartTime + doorConfig.openDuration + 1000; // +1s safety
  
  // Start motor (open direction)
  analogWrite(ENA_PIN, doorConfig.motorSpeed);
  if (!doorConfig.reversed) {
    digitalWrite(MOTOR_PIN1, HIGH);
    digitalWrite(MOTOR_PIN2, LOW);
  } else {
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, HIGH);
  }
  
  return true;
}

bool closeDoor() {
  if (isMoving) {
    Serial.println("[DOOR] Already moving");
    return false;
  }
  
  if (!isDoorOpen) {
    Serial.println("[DOOR] Already closed");
    return false;
  }
  
  Serial.println("[DOOR] Closing - Duration: " + String(doorConfig.closeDuration) + "ms, Speed: " + String(doorConfig.motorSpeed));
  
  isMoving = true;
  doorState = DOOR_CLOSING;
  motorStartTime = millis();
  movementTimeout = motorStartTime + doorConfig.closeDuration + 1000; // +1s safety
  
  // Start motor (close direction)
  analogWrite(ENA_PIN, doorConfig.motorSpeed);
  if (!doorConfig.reversed) {
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, HIGH);
  } else {
    digitalWrite(MOTOR_PIN1, HIGH);
    digitalWrite(MOTOR_PIN2, LOW);
  }
  
  return true;
}

bool toggleDoor() {
  return isDoorOpen ? closeDoor() : openDoor();
}

void stopMotor() {
  digitalWrite(MOTOR_PIN1, LOW);
  digitalWrite(MOTOR_PIN2, LOW);
  analogWrite(ENA_PIN, 0);
}

// ===== DOOR MOVEMENT HANDLER =====
void handleDoorMovement() {
  if (!isMoving) return;
  
  unsigned long elapsedTime = millis() - motorStartTime;
  unsigned long targetDuration = (doorState == DOOR_OPENING) ? doorConfig.openDuration : doorConfig.closeDuration;
  
  // Safety timeout check
  if (millis() > movementTimeout) {
    Serial.println("[DOOR] ⚠️ Safety timeout!");
    stopMotor();
    isMoving = false;
    sendDoorStatus();
    return;
  }
  
  // Normal completion
  if (elapsedTime >= targetDuration) {
    stopMotor();
    
    if (doorState == DOOR_OPENING) {
      isDoorOpen = true;
      doorState = DOOR_OPEN;
      lastMotionTime = millis();
      Serial.println("[DOOR] ✓ Opened");
    } else {
      isDoorOpen = false;
      doorState = DOOR_CLOSED;
      Serial.println("[DOOR] ✓ Closed");
    }
    
    isMoving = false;
    saveDoorState();
    sendDoorStatus();
  }
  
  // Enhanced PIR safety check during closing
  if (doorState == DOOR_CLOSING && doorConfig.autoMode && isMotionDetected()) {
    Serial.println("[SAFETY] Motion detected during closing - stopping and reopening");
    stopMotor();
    delay(200);
    
    // Reopen immediately
    isMoving = true;
    doorState = DOOR_OPENING;
    motorStartTime = millis();
    movementTimeout = motorStartTime + elapsedTime + 1000; // Run back for same time + safety
    
    // Reverse direction
    if (!doorConfig.reversed) {
      digitalWrite(MOTOR_PIN1, HIGH);
      digitalWrite(MOTOR_PIN2, LOW);
    } else {
      digitalWrite(MOTOR_PIN1, LOW);
      digitalWrite(MOTOR_PIN2, HIGH);
    }
    analogWrite(ENA_PIN, doorConfig.motorSpeed);
    
    lastMotionTime = millis();
  }
}

// ===== ENHANCED PIR SENSOR FUNCTIONS =====
bool readPIR1() {
  if (!doorConfig.pirConfig.pir1Enabled) return false;
  
  bool currentState = digitalRead(PIR1_PIN) == HIGH;
  unsigned long now = millis();
  
  // Debounce processing
  if (currentState != pirState.pir1LastState) {
    pirState.pir1LastDebounce = now;
  }
  
  if ((now - pirState.pir1LastDebounce) > doorConfig.pirConfig.pirDebounceTime) {
    if (currentState != pirState.pir1CurrentState) {
      pirState.pir1CurrentState = currentState;
      
      if (currentState) {
        pirState.pir1LastTrigger = now;
        pirState.pir1TriggerCount++;
        
        // Sensitivity-based filtering
        int requiredTriggers = 11 - doorConfig.pirConfig.pir1Sensitivity; // 1 for sensitivity 10, 10 for sensitivity 1
        if (pirState.pir1TriggerCount >= requiredTriggers) {
          Serial.println("[PIR1] Motion detected (sensitivity: " + String(doorConfig.pirConfig.pir1Sensitivity) + "/10)");
          pirState.pir1TriggerCount = 0; // Reset counter
          return true;
        }
      }
    }
  }
  
  pirState.pir1LastState = currentState;
  
  // Reset trigger count if no motion for timeout period
  if ((now - pirState.pir1LastTrigger) > doorConfig.pirConfig.motionTimeout) {
    pirState.pir1TriggerCount = 0;
  }
  
  return pirState.pir1CurrentState && (now - pirState.pir1LastTrigger) <= doorConfig.pirConfig.motionTimeout;
}

bool readPIR2() {
  if (!doorConfig.pirConfig.pir2Enabled) return false;
  
  bool currentState = digitalRead(PIR2_PIN) == HIGH;
  unsigned long now = millis();
  
  // Debounce processing
  if (currentState != pirState.pir2LastState) {
    pirState.pir2LastDebounce = now;
  }
  
  if ((now - pirState.pir2LastDebounce) > doorConfig.pirConfig.pirDebounceTime) {
    if (currentState != pirState.pir2CurrentState) {
      pirState.pir2CurrentState = currentState;
      
      if (currentState) {
        pirState.pir2LastTrigger = now;
        pirState.pir2TriggerCount++;
        
        // Sensitivity-based filtering
        int requiredTriggers = 11 - doorConfig.pirConfig.pir2Sensitivity; // 1 for sensitivity 10, 10 for sensitivity 1
        if (pirState.pir2TriggerCount >= requiredTriggers) {
          Serial.println("[PIR2] Motion detected (sensitivity: " + String(doorConfig.pirConfig.pir2Sensitivity) + "/10)");
          pirState.pir2TriggerCount = 0; // Reset counter
          return true;
        }
      }
    }
  }
  
  pirState.pir2LastState = currentState;
  
  // Reset trigger count if no motion for timeout period
  if ((now - pirState.pir2LastTrigger) > doorConfig.pirConfig.motionTimeout) {
    pirState.pir2TriggerCount = 0;
  }
  
  return pirState.pir2CurrentState && (now - pirState.pir2LastTrigger) <= doorConfig.pirConfig.motionTimeout;
}

bool isMotionDetected() {
  bool pir1Motion = readPIR1();
  bool pir2Motion = readPIR2();
  
  if (doorConfig.pirConfig.requireBothSensors) {
    return pir1Motion && pir2Motion;
  } else {
    return pir1Motion || pir2Motion;
  }
}

void handleAutoOperation() {
  if (!doorConfig.autoMode || isMoving) return;
  
  bool motionDetected = isMotionDetected();
  
  // Smart detection logic
  if (doorConfig.pirConfig.smartDetection) {
    unsigned long now = millis();
    
    if (motionDetected && !pirState.motionActive) {
      pirState.motionActive = true;
      pirState.motionStartTime = now;
    } else if (!motionDetected && pirState.motionActive) {
      // Check if motion stopped for sufficient time
      if ((now - pirState.motionStartTime) > doorConfig.pirConfig.motionTimeout) {
        pirState.motionActive = false;
      }
    }
    
    motionDetected = pirState.motionActive;
  }
  
  // Open door if motion detected and door is closed
  if (!isDoorOpen && motionDetected) {
    Serial.println("[AUTO] Motion detected, opening door");
    openDoor();
  }
  
  // Keep door open if motion continues
  if (isDoorOpen && motionDetected) {
    lastMotionTime = millis();
  }
  
  // Close door after timeout
  if (isDoorOpen && !motionDetected && 
      (millis() - lastMotionTime >= doorConfig.waitBeforeClose)) {
    Serial.println("[AUTO] Timeout reached, closing door");
    closeDoor();
  }
}

// ===== MANUAL BUTTON =====
void handleManualButton() {
  bool currentButtonState = digitalRead(BUTTON_PIN);
  
  if (currentButtonState != lastButtonState) {
    if (millis() - lastButtonTime > buttonDebounce) {
      if (currentButtonState == LOW) { // Button pressed
        Serial.println("[BUTTON] Manual toggle");
        toggleDoor();
      }
      lastButtonTime = millis();
    }
    lastButtonState = currentButtonState;
  }
}

// ===== WEBSOCKET COMMUNICATION =====
void sendDeviceOnline() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["serialNumber"] = DEVICE_SERIAL;
  doc["deviceType"] = DEVICE_TYPE;
  doc["door_type"] = DOOR_TYPE;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["connection_type"] = "direct";
  doc["door_id"] = DOOR_ID;
  doc["features"] = "PIR_CONFIGURABLE,PIR_SENSITIVITY,AUTO_MODE,MANUAL_OVERRIDE,WEB_CONFIG";
  doc["pir_config"] = true;
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"device_online\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  
  Serial.println("[WS] Device online sent with PIR config support");
}

void sendCommandResponse(String action, bool success, String result) {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["success"] = success;
  doc["result"] = result;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["command"] = action;
  doc["door_state"] = getStateString();
  doc["door_type"] = DOOR_TYPE;
  doc["auto_mode"] = doorConfig.autoMode;
  doc["pir1_enabled"] = doorConfig.pirConfig.pir1Enabled;
  doc["pir2_enabled"] = doorConfig.pirConfig.pir2Enabled;
  doc["pir1_sensitivity"] = doorConfig.pirConfig.pir1Sensitivity;
  doc["pir2_sensitivity"] = doorConfig.pirConfig.pir2Sensitivity;
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"command_response\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  
  Serial.println("[WS] Command response sent: " + result);
}

void sendDoorStatus() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["door_state"] = getStateString();
  doc["door_type"] = DOOR_TYPE;
  doc["is_moving"] = isMoving;
  doc["auto_mode"] = doorConfig.autoMode;
  doc["pir1_state"] = readPIR1();
  doc["pir2_state"] = readPIR2();
  doc["pir1_enabled"] = doorConfig.pirConfig.pir1Enabled;
  doc["pir2_enabled"] = doorConfig.pirConfig.pir2Enabled;
  doc["pir1_sensitivity"] = doorConfig.pirConfig.pir1Sensitivity;
  doc["pir2_sensitivity"] = doorConfig.pirConfig.pir2Sensitivity;
  doc["motion_active"] = pirState.motionActive;
  doc["motor_speed"] = doorConfig.motorSpeed;
  doc["online"] = true;
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"deviceStatus\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
}

String getStateString() {
  switch(doorState) {
    case DOOR_CLOSED: return "closed";
    case DOOR_OPENING: return "opening";
    case DOOR_OPEN: return "open";
    case DOOR_CLOSING: return "closing";
    default: return "unknown";
  }
}

// ===== EEPROM FUNCTIONS =====
void loadDoorConfig() {
  uint8_t initFlag;
  EEPROM.get(ADDR_INIT_FLAG, initFlag);
  
  if (initFlag == 0xCC) {
    EEPROM.get(ADDR_MOTOR_SPEED, doorConfig.motorSpeed);
    EEPROM.get(ADDR_OPEN_DURATION, doorConfig.openDuration);
    EEPROM.get(ADDR_WAIT_TIME, doorConfig.waitBeforeClose);
    EEPROM.get(ADDR_AUTO_MODE, doorConfig.autoMode);
    EEPROM.get(ADDR_PIR1_ENABLED, doorConfig.pirConfig.pir1Enabled);
    EEPROM.get(ADDR_PIR2_ENABLED, doorConfig.pirConfig.pir2Enabled);
    EEPROM.get(ADDR_PIR1_SENSITIVITY, doorConfig.pirConfig.pir1Sensitivity);
    EEPROM.get(ADDR_PIR2_SENSITIVITY, doorConfig.pirConfig.pir2Sensitivity);
    EEPROM.get(ADDR_PIR_DEBOUNCE, doorConfig.pirConfig.pirDebounceTime);
    EEPROM.get(ADDR_MOTION_TIMEOUT, doorConfig.pirConfig.motionTimeout);
    Serial.println("[CONFIG] Loaded from EEPROM including PIR settings");
  } else {
    Serial.println("[CONFIG] Using defaults including PIR settings");
    saveDoorConfig();
  }
  
  // Validate ranges
  if (doorConfig.motorSpeed < 50 || doorConfig.motorSpeed > 255) doorConfig.motorSpeed = 200;
  if (doorConfig.openDuration < 500 || doorConfig.openDuration > 10000) doorConfig.openDuration = 3000;
  if (doorConfig.closeDuration < 500 || doorConfig.closeDuration > 10000) doorConfig.closeDuration = 3000;
  if (doorConfig.waitBeforeClose < 1000 || doorConfig.waitBeforeClose > 30000) doorConfig.waitBeforeClose = 5000;
  if (doorConfig.pirConfig.pir1Sensitivity < 1 || doorConfig.pirConfig.pir1Sensitivity > 10) doorConfig.pirConfig.pir1Sensitivity = 7;
  if (doorConfig.pirConfig.pir2Sensitivity < 1 || doorConfig.pirConfig.pir2Sensitivity > 10) doorConfig.pirConfig.pir2Sensitivity = 7;
  if (doorConfig.pirConfig.pirDebounceTime < 100 || doorConfig.pirConfig.pirDebounceTime > 2000) doorConfig.pirConfig.pirDebounceTime = 200;
  if (doorConfig.pirConfig.motionTimeout < 500 || doorConfig.pirConfig.motionTimeout > 5000) doorConfig.pirConfig.motionTimeout = 1000;
  
  Serial.println("[CONFIG] Motor Speed: " + String(doorConfig.motorSpeed));
  Serial.println("[CONFIG] Open Duration: " + String(doorConfig.openDuration) + "ms");
  Serial.println("[CONFIG] Auto Mode: " + String(doorConfig.autoMode ? "ON" : "OFF"));
}

void saveDoorConfig() {
  EEPROM.put(ADDR_MOTOR_SPEED, doorConfig.motorSpeed);
  EEPROM.put(ADDR_OPEN_DURATION, doorConfig.openDuration);
  EEPROM.put(ADDR_WAIT_TIME, doorConfig.waitBeforeClose);
  EEPROM.put(ADDR_AUTO_MODE, doorConfig.autoMode);
  EEPROM.put(ADDR_PIR1_ENABLED, doorConfig.pirConfig.pir1Enabled);
  EEPROM.put(ADDR_PIR2_ENABLED, doorConfig.pirConfig.pir2Enabled);
  EEPROM.put(ADDR_PIR1_SENSITIVITY, doorConfig.pirConfig.pir1Sensitivity);
  EEPROM.put(ADDR_PIR2_SENSITIVITY, doorConfig.pirConfig.pir2Sensitivity);
  EEPROM.put(ADDR_PIR_DEBOUNCE, doorConfig.pirConfig.pirDebounceTime);
  EEPROM.put(ADDR_MOTION_TIMEOUT, doorConfig.pirConfig.motionTimeout);
  EEPROM.put(ADDR_INIT_FLAG, 0xCC);
  EEPROM.commit();
}

void loadDoorState() {
  EEPROM.get(ADDR_DOOR_STATE, isDoorOpen);
  doorState = isDoorOpen ? DOOR_OPEN : DOOR_CLOSED;
  Serial.println("[STATE] Door is " + String(isDoorOpen ? "OPEN" : "CLOSED"));
}

void saveDoorState() {
  EEPROM.put(ADDR_DOOR_STATE, isDoorOpen);
  EEPROM.commit();
}

void checkConnection() {
  if (socketConnected && (millis() - lastPingResponse > 120000)) {
    Serial.println("[WS] Connection timeout");
    webSocket.disconnect();
    socketConnected = false;
  }
}

void loop() {
  // Handle config mode
  if (configMode) {
    handleConfigMode();
    return;
  }
  
  // Normal operation
  webSocket.loop();
  
  // ✅ CRITICAL: Handle door movement every loop
  handleDoorMovement();
  
  // Handle auto operation
  handleAutoOperation();
  
  // Handle manual button
  if (!isMoving) {
    handleManualButton();
  }
  
  // Periodic status updates
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 60000) {
    if (socketConnected) sendDoorStatus();
    lastStatusCheck = millis();
  }
  
  // Connection check
  static unsigned long lastConnCheck = 0;
  if (millis() - lastConnCheck > 30000) {
    checkConnection();
    lastConnCheck = millis();
  }
  
  // Manual ping
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 30000) {
    webSocket.sendTXT("2");
    lastManualPing = millis();
  }
  
  // Status print with PIR info
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 45000) {
    Serial.println("[STATUS] Socket:" + String(socketConnected ? "OK" : "NO") + 
                   " | Door:" + getStateString() + 
                   " | Auto:" + String(doorConfig.autoMode ? "ON" : "OFF") +
                   " | PIR1:" + String(readPIR1() ? "MOTION" : "CLEAR") + "(" + String(doorConfig.pirConfig.pir1Sensitivity) + "/10)" +
                   " | PIR2:" + String(readPIR2() ? "MOTION" : "CLEAR") + "(" + String(doorConfig.pirConfig.pir2Sensitivity) + "/10)");
    lastStatusPrint = millis();
  }
  
  yield();
  delay(10);
}