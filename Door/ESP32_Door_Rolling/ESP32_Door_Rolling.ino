// ========================================
// ESP32 ROLLING DOOR - COMPLETE VERSION v5.0.2
// Direct WebSocket connection with WiFi UDP config + Web UI
// ========================================

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Stepper.h>
#include <EEPROM.h>

#define FIRMWARE_VERSION "5.0.2"
#define DEVICE_TYPE "ESP32_ROLLING_DOOR"
#define DOOR_ID 9

// ✅ DEVICE CONFIGURATION (can be updated via UDP/Web)
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVVSBGRTM0TRFW";
String WIFI_SSID = "Anh Tuan";
String WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;

// ✅ HARDWARE PINS
#define SS_PIN 21
#define RST_PIN 22
#define BUTTON_CW_PIN 25     // Open button
#define BUTTON_CCW_PIN 26    // Close button
#define BUTTON_CONFIG_PIN 0  // Config button (GPIO0)

// ✅ STEPPER MOTOR CONFIGURATION
const int STEPS_PER_REVOLUTION = 2048;
const int DEFAULT_OPEN_ROUNDS = 2;
const int DEFAULT_CLOSED_ROUNDS = 0;
const int DEFAULT_MOTOR_SPEED = 10;

// ✅ EEPROM ADDRESSES
#define EEPROM_SIZE 512
#define ADDR_DOOR_STATE 0
#define ADDR_CLOSED_ROUNDS 4
#define ADDR_OPEN_ROUNDS 8
#define ADDR_MOTOR_SPEED 12
#define ADDR_INIT_FLAG 16
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 164
#define WIFI_SERIAL_ADDR 228
#define WIFI_CONFIG_FLAG_ADDR 292

// ✅ WIFI CONFIG
WiFiUDP udp;
WebServer webServer(80);
const int UDP_PORT = 12345;
bool configMode = false;
unsigned long configModeStart = 0;
const unsigned long CONFIG_TIMEOUT = 300000; // 5 minutes

// ✅ DOOR CONFIGURATION STRUCTURE
struct RollingDoorConfig {
  int openRounds;
  int closedRounds;
  int motorSpeed;
  int stepsPerRound;
  bool reversed;
  int maxRounds;
  String doorType;
};

RollingDoorConfig doorConfig;

// ✅ HARDWARE OBJECTS
Stepper myStepper(STEPS_PER_REVOLUTION, 16, 14, 15, 13);
MFRC522 mfrc522(SS_PIN, RST_PIN);
WebSocketsClient webSocket;

// ✅ DOOR STATE
enum DoorState { 
  DOOR_CLOSED = 0, 
  DOOR_OPENING = 1, 
  DOOR_OPEN = 2, 
  DOOR_CLOSING = 3 
};

DoorState doorState = DOOR_CLOSED;
bool isMoving = false;
bool isDoorOpen = false;
int currentRounds = 0;

// ✅ BUTTON HANDLING
int cwButtonState = HIGH;
int ccwButtonState = HIGH;
int lastCWButtonState = HIGH;
int lastCCWButtonState = HIGH;
unsigned long lastCWDebounceTime = 0;
unsigned long lastCCWDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ✅ WEBSOCKET STATUS
bool socketConnected = false;
unsigned long lastPingResponse = 0;
unsigned long lastStatusSend = 0;

// ✅ AUTHORIZED RFID TAGS
String authorizedTags[] = {
  "63 83 41 10",
  "7E 32 30 00", 
  "FC F8 45 03",
  "95 79 1C 53",
  "F5 BC 0C 53",
  "F7 73 A1 D5"
};
const int totalAuthorizedTags = 6;

// ✅ LED BUILTIN (ESP32 LED is typically active HIGH on pin 2)
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP32 ROLLING DOOR v5.0.2 ===");
  Serial.println("Device: " + DEVICE_SERIAL);
  Serial.println("Type: " + String(DEVICE_TYPE));
  Serial.println("Door ID: " + String(DOOR_ID));
  
  // ✅ INITIALIZE EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // ✅ INITIALIZE HARDWARE
  initializeHardware();
  
  // ✅ CHECK FOR CONFIG MODE (hold config button during boot)
  pinMode(BUTTON_CONFIG_PIN, INPUT_PULLUP);
  delay(100);
  if (digitalRead(BUTTON_CONFIG_PIN) == LOW) {
    Serial.println("[CONFIG] Config button pressed, entering WiFi config mode...");
    startConfigMode();
  } else {
    // ✅ NORMAL OPERATION MODE
    loadDoorConfig();
    loadDoorState();
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      setupWebSocket();
    }
  }
  
  Serial.println("✓ Rolling Door Ready");
  Serial.println("================================\n");
}

void initializeHardware() {
  // RFID
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("[RFID] ✓ MFRC522 initialized");
  
  // Stepper motor
  myStepper.setSpeed(DEFAULT_MOTOR_SPEED);
  
  // Buttons
  pinMode(BUTTON_CW_PIN, INPUT_PULLUP);
  pinMode(BUTTON_CCW_PIN, INPUT_PULLUP);
  
  // Motor pins
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);
  
  // LED for config mode indication (ESP32 LED is typically active HIGH)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // Turn OFF
  
  Serial.println("[MOTOR] ✓ Stepper motor initialized");
  Serial.println("[BUTTONS] ✓ Manual buttons initialized");
  Serial.println("[LED] ✓ Built-in LED initialized (Pin " + String(LED_BUILTIN) + ")");
}

// ✅ WIFI CONFIG MODE FUNCTIONS
void startConfigMode() {
  configMode = true;
  configModeStart = millis();
  
  Serial.println("[CONFIG] Starting WiFi AP for configuration...");
  
  // Create AP
  String apName = String(DEVICE_TYPE) + "_" + String(DOOR_ID);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str(), "12345678");
  
  IPAddress apIP = WiFi.softAPIP();
  Serial.println("[CONFIG] AP Started:");
  Serial.println("[CONFIG] SSID: " + apName);
  Serial.println("[CONFIG] Password: 12345678");
  Serial.println("[CONFIG] IP: " + apIP.toString());
  Serial.println("[CONFIG] Web Interface: http://" + apIP.toString());
  Serial.println("[CONFIG] UDP Port: " + String(UDP_PORT));
  
  // Start services
  udp.begin(UDP_PORT);
  setupWebServer();
  webServer.begin();
  
  Serial.println("[CONFIG] ✓ Web server started");
}

void setupWebServer() {
  // Main config page
  webServer.on("/", handleConfigPage);
  
  // API endpoints
  webServer.on("/save", HTTP_POST, handleSaveConfig);
  webServer.on("/status", HTTP_GET, handleStatus);
  
  // Handle not found
  webServer.onNotFound(handleNotFound);
}

void handleConfigPage() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Rolling Door Config</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh; padding: 20px; color: #333;
        }
        .container { 
            max-width: 500px; margin: 0 auto; 
            background: white; border-radius: 15px; 
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
            overflow: hidden; animation: slideUp 0.5s ease;
        }
        @keyframes slideUp { from { opacity: 0; transform: translateY(30px); } }
        .header { 
            background: linear-gradient(135deg, #FF6B6B, #FF8E53);
            color: white; padding: 25px; text-align: center;
        }
        .header h1 { font-size: 24px; margin-bottom: 8px; }
        .header p { opacity: 0.9; font-size: 14px; }
        .form { padding: 30px; }
        .field { margin-bottom: 20px; }
        .field label { 
            display: block; margin-bottom: 8px; 
            font-weight: 600; color: #555;
        }
        .field input { 
            width: 100%; padding: 12px 15px; border: 2px solid #e1e5e9;
            border-radius: 8px; font-size: 16px; transition: all 0.3s;
        }
        .field input:focus { 
            outline: none; border-color: #FF6B6B; 
            box-shadow: 0 0 0 3px rgba(255,107,107,0.1);
        }
        .btn { 
            width: 100%; padding: 15px; background: linear-gradient(135deg, #FF6B6B, #FF8E53);
            color: white; border: none; border-radius: 8px; 
            font-size: 16px; font-weight: 600; cursor: pointer;
            transition: all 0.3s; margin-top: 10px;
        }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(255,107,107,0.3); }
        .status { 
            padding: 15px; margin: 15px 0; border-radius: 8px; 
            text-align: center; font-weight: 600;
        }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .info { 
            background: #e3f2fd; color: #0d47a1; border: 1px solid #bbdefb;
            margin-bottom: 20px; padding: 15px; border-radius: 8px;
        }
        .footer { 
            background: #f8f9fa; padding: 20px; text-align: center; 
            color: #666; font-size: 12px; border-top: 1px solid #e9ecef;
        }
        @media (max-width: 600px) {
            .container { margin: 10px; border-radius: 10px; }
            .header { padding: 20px; }
            .form { padding: 20px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🏠 Rolling Door Config</h1>
            <p>ESP32 WiFi Configuration</p>
        </div>
        
        <div class="form">
            <div class="info">
                <strong>📋 Instructions:</strong><br>
                1. Connect to this WiFi network<br>
                2. Enter your home WiFi credentials<br>
                3. Optionally update device serial<br>
                4. Click Save to apply settings
            </div>
            
            <div id="status"></div>
            
            <form id="configForm">
                <div class="field">
                    <label for="ssid">🌐 WiFi Network Name (SSID)</label>
                    <input type="text" id="ssid" name="ssid" required maxlength="31" 
                           placeholder="Enter your WiFi name">
                </div>
                
                <div class="field">
                    <label for="password">🔐 WiFi Password</label>
                    <input type="password" id="password" name="password" required maxlength="31"
                           placeholder="Enter your WiFi password">
                </div>
                
                <div class="field">
                    <label for="serial">🏷️ Device Serial (Optional)</label>
                    <input type="text" id="serial" name="serial" maxlength="31"
                           placeholder="Leave empty to keep current">
                </div>
                
                <button type="submit" class="btn">💾 Save Configuration</button>
            </form>
        </div>
        
        <div class="footer">
            ESP32 Rolling Door v5.0.2 | Device will restart after saving
        </div>
    </div>

    <script>
        document.getElementById('configForm').addEventListener('submit', async function(e) {
            e.preventDefault();
            
            const statusDiv = document.getElementById('status');
            const formData = new FormData(this);
            const submitBtn = this.querySelector('button[type="submit"]');
            
            // Validate inputs
            const ssid = formData.get('ssid').trim();
            const password = formData.get('password').trim();
            
            if (!ssid || !password) {
                statusDiv.innerHTML = '<div class="status error">❌ SSID and Password are required</div>';
                return;
            }
            
            if (ssid.length > 31 || password.length > 31) {
                statusDiv.innerHTML = '<div class="status error">❌ SSID and Password must be 31 characters or less</div>';
                return;
            }
            
            // Show loading
            submitBtn.disabled = true;
            submitBtn.textContent = '⏳ Saving...';
            statusDiv.innerHTML = '<div class="status">⏳ Saving configuration...</div>';
            
            try {
                const response = await fetch('/save', {
                    method: 'POST',
                    body: formData
                });
                
                const result = await response.json();
                
                if (result.success) {
                    statusDiv.innerHTML = '<div class="status success">✅ ' + result.message + '</div>';
                    setTimeout(() => {
                        statusDiv.innerHTML = '<div class="status">🔄 Device is restarting... Please reconnect to your home WiFi.</div>';
                    }, 2000);
                } else {
                    statusDiv.innerHTML = '<div class="status error">❌ ' + result.message + '</div>';
                    submitBtn.disabled = false;
                    submitBtn.textContent = '💾 Save Configuration';
                }
            } catch (error) {
                statusDiv.innerHTML = '<div class="status error">❌ Connection error: ' + error.message + '</div>';
                submitBtn.disabled = false;
                submitBtn.textContent = '💾 Save Configuration';
            }
        });
        
        // Auto-focus first input
        document.getElementById('ssid').focus();
    </script>
</body>
</html>
)";
  
  webServer.send(200, "text/html", html);
}

void handleSaveConfig() {
  String response;
  bool success = false;
  
  if (webServer.hasArg("ssid") && webServer.hasArg("password")) {
    String newSSID = webServer.arg("ssid");
    String newPassword = webServer.arg("password");
    String newSerial = webServer.arg("serial");
    
    // Validate input length
    if (newSSID.length() > 0 && newSSID.length() <= 31 && 
        newPassword.length() > 0 && newPassword.length() <= 31) {
      
      // Update serial if provided
      if (newSerial.length() > 0 && newSerial.length() <= 31) {
        DEVICE_SERIAL = newSerial;
      }
      
      // Save configuration
      saveWiFiConfig(newSSID, newPassword, newSerial);
      
      success = true;
      response = "{\"success\":true,\"message\":\"Configuration saved successfully! Device will restart in 3 seconds.\"}";
      
      Serial.println("[CONFIG] ✓ Web config saved");
      Serial.println("[CONFIG] SSID: " + newSSID);
      if (newSerial.length() > 0) {
        Serial.println("[CONFIG] New Serial: " + newSerial);
      }
      
    } else {
      response = "{\"success\":false,\"message\":\"Invalid SSID or password length (max 31 characters)\"}";
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

void handleStatus() {
  StaticJsonDocument<256> doc;
  doc["device_type"] = DEVICE_TYPE;
  doc["device_serial"] = DEVICE_SERIAL;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void handleNotFound() {
  webServer.send(404, "text/plain", "Not found - Please go to http://" + WiFi.softAPIP().toString());
}

void handleConfigMode() {
  if (!configMode) return;
  
  // Timeout check
  if (millis() - configModeStart > CONFIG_TIMEOUT) {
    Serial.println("[CONFIG] Timeout, restarting...");
    ESP.restart();
  }
  
  // Blink LED to indicate config mode (ESP32 LED is typically active HIGH)
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 500) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastBlink = millis();
  }
  
  // Handle web server
  webServer.handleClient();
  
  // Handle UDP packets (backward compatibility)
  handleUDPConfig();
}

void handleUDPConfig() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    Serial.println("[CONFIG] Received UDP packet: " + String(packetSize) + " bytes");
    
    char packet[256];
    int len = udp.read(packet, 255);
    if (len > 0) {
      packet[len] = 0;
      
      String message = String(packet);
      Serial.println("[CONFIG] UDP Message: " + message);
      
      // Parse JSON config
      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, message) == DeserializationError::Ok) {
        String newSSID = doc["ssid"].as<String>();
        String newPassword = doc["password"].as<String>();
        String newSerial = doc["serial"].as<String>();
        
        if (newSSID.length() > 0 && newSSID.length() <= 31 && 
            newPassword.length() > 0 && newPassword.length() <= 31) {
          
          if (newSerial.length() > 0 && newSerial.length() <= 31) {
            DEVICE_SERIAL = newSerial;
          }
          
          // Save to EEPROM
          saveWiFiConfig(newSSID, newPassword, newSerial);
          
          // Send success response
          String response = "{\"success\":true,\"message\":\"WiFi config saved, restarting...\"}";
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write((uint8_t*)response.c_str(), response.length());
          udp.endPacket();
          
          Serial.println("[CONFIG] ✓ UDP Config saved, restarting...");
          delay(2000);
          ESP.restart();
        } else {
          // Send error response
          String response = "{\"success\":false,\"message\":\"Invalid SSID or password length\"}";
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write((uint8_t*)response.c_str(), response.length());
          udp.endPacket();
        }
      } else {
        String response = "{\"success\":false,\"message\":\"Invalid JSON format\"}";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((uint8_t*)response.c_str(), response.length());
        udp.endPacket();
      }
    }
  }
}

void saveWiFiConfig(String ssid, String password, String serial) {
  // Input validation
  if (ssid.length() > 31) ssid = ssid.substring(0, 31);
  if (password.length() > 31) password = password.substring(0, 31);
  if (serial.length() > 31) serial = serial.substring(0, 31);
  
  // Clear and save SSID
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, i < ssid.length() ? ssid[i] : 0);
  }
  // Clear and save password
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_PASS_ADDR + i, i < password.length() ? password[i] : 0);
  }
  // Clear and save serial
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_SERIAL_ADDR + i, i < serial.length() ? serial[i] : 0);
  }
  EEPROM.write(WIFI_CONFIG_FLAG_ADDR, 0xAB); // Config saved flag
  EEPROM.commit();
  
  Serial.println("[CONFIG] WiFi config saved to EEPROM");
}

bool loadWiFiConfig() {
  if (EEPROM.read(WIFI_CONFIG_FLAG_ADDR) != 0xAB) {
    Serial.println("[CONFIG] No saved WiFi config found");
    return false;
  }
  
  char ssid[33] = {0};
  char password[33] = {0};
  char serial[33] = {0};
  
  for (int i = 0; i < 32; i++) {
    ssid[i] = EEPROM.read(WIFI_SSID_ADDR + i);
    password[i] = EEPROM.read(WIFI_PASS_ADDR + i);
    serial[i] = EEPROM.read(WIFI_SERIAL_ADDR + i);
  }
  
  // Update global variables
  WIFI_SSID = String(ssid);
  WIFI_PASSWORD = String(password);
  
  if (strlen(serial) > 0) {
    DEVICE_SERIAL = String(serial);
  }
  
  Serial.println("[CONFIG] ✓ WiFi config loaded from EEPROM");
  Serial.println("[CONFIG] SSID: " + WIFI_SSID);
  Serial.println("[CONFIG] Serial: " + DEVICE_SERIAL);
  
  return true;
}

void setupWiFi() {
  // Try to load saved config first
  if (!loadWiFiConfig()) {
    Serial.println("[WiFi] No saved config, using defaults");
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
    Serial.println("[WiFi] Signal: " + String(WiFi.RSSI()) + "dBm");
    
    // Turn off LED when connected
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    Serial.println(" ✗ FAILED");
    Serial.println("[WiFi] Starting config mode...");
    startConfigMode();
  }
}

void setupWebSocket() {
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                DEVICE_SERIAL + "&isIoTDevice=true";
  
  Serial.println("[WS] Connecting to " + WEBSOCKET_HOST + ":" + String(WEBSOCKET_PORT));
  
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(25000, 5000, 2);
  
  String userAgent = "ESP32-Rolling-Door/5.0.2";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[WS] Setup complete");
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
      
    case WStype_ERROR:
      Serial.println("[WS] ✗ ERROR");
      break;
      
    case WStype_PONG:
      lastPingResponse = millis();
      break;
  }
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  
  if (type == '0') {
    // Connection established
    
  } else if (type == '2') {
    webSocket.sendTXT("3");
    lastPingResponse = millis();
    
  } else if (type == '3') {
    lastPingResponse = millis();
    
  } else if (type == '4') {
    String socketIOData = message.substring(1);
    handleSocketIOMessage(socketIOData);
  }
}

void handleSocketIOMessage(String data) {
  if (data.length() < 1) return;
  
  char type = data.charAt(0);
  
  if (type == '2') {
    String eventData = data.substring(1);
    handleSocketIOEvent(eventData);
  }
}

void handleSocketIOEvent(String eventData) {
  if (eventData.indexOf("command") != -1) {
    parseAndExecuteCommand(eventData);
    
  } else if (eventData.indexOf("config") != -1) {
    parseAndExecuteConfig(eventData);
    
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
    
  } else if (action == "get_config" || action == "CFG") {
    sendDoorConfig();
    return;
    
  } else {
    result = "unknown_command";
  }
  
  sendCommandResponse(action, success, result);
}

void parseAndExecuteConfig(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx == -1 || endIdx == -1) return;
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, jsonString) != DeserializationError::Ok) return;
  
  String configType = doc["config_type"].as<String>();
  String serialNumber = doc["serialNumber"].as<String>();
  
  if (serialNumber != DEVICE_SERIAL) return;
  
  Serial.println("[CONFIG] Processing: " + configType);
  
  bool success = false;
  String result = "unknown";
  
  if (configType == "stepper_config") {
    int openRounds = doc["open_rounds"] | doorConfig.openRounds;
    int closedRounds = doc["closed_rounds"] | doorConfig.closedRounds;
    int motorSpeed = doc["motor_speed"] | doorConfig.motorSpeed;
    
    if (openRounds >= 1 && openRounds <= 10 && 
        closedRounds >= 0 && closedRounds <= 10 &&
        motorSpeed >= 5 && motorSpeed <= 50) {
      
      doorConfig.openRounds = openRounds;
      doorConfig.closedRounds = closedRounds;
      doorConfig.motorSpeed = motorSpeed;
      
      myStepper.setSpeed(motorSpeed);
      saveDoorConfig();
      
      success = true;
      result = "config_updated";
    } else {
      result = "invalid_parameters";
    }
    
  } else if (configType == "rfid_tags") {
    success = true;
    result = "rfid_config_received";
    
  } else if (configType == "get_config") {
    sendDoorConfig();
    return;
  }
  
  sendConfigResponse(configType, success, result);
}

// ✅ DOOR CONTROL FUNCTIONS
bool openDoor() {
  if (isMoving || isDoorOpen) return false;
  
  isMoving = true;
  doorState = DOOR_OPENING;
  
  Serial.println("[DOOR] Opening - " + String(doorConfig.openRounds) + " rounds CW");
  
  int steps = doorConfig.openRounds * doorConfig.stepsPerRound;
  if (doorConfig.reversed) steps = -steps;
  
  myStepper.step(steps);
  turnOffMotor();
  
  isDoorOpen = true;
  doorState = DOOR_OPEN;
  currentRounds = doorConfig.openRounds;
  isMoving = false;
  
  saveDoorState();
  sendDoorStatus();
  
  Serial.println("[DOOR] ✓ Opened");
  return true;
}

bool closeDoor() {
  if (isMoving || !isDoorOpen) return false;
  
  isMoving = true;
  doorState = DOOR_CLOSING;
  
  Serial.println("[DOOR] Closing - " + String(doorConfig.openRounds) + " rounds CCW");
  
  int steps = doorConfig.openRounds * doorConfig.stepsPerRound;
  if (!doorConfig.reversed) steps = -steps;
  
  myStepper.step(steps);
  turnOffMotor();
  
  isDoorOpen = false;
  doorState = DOOR_CLOSED;
  currentRounds = doorConfig.closedRounds;
  isMoving = false;
  
  saveDoorState();
  sendDoorStatus();
  
  Serial.println("[DOOR] ✓ Closed");
  return true;
}

bool toggleDoor() {
  return isDoorOpen ? closeDoor() : openDoor();
}

void turnOffMotor() {
  digitalWrite(13, LOW);
  digitalWrite(14, LOW);
  digitalWrite(15, LOW);
  digitalWrite(16, LOW);
}

// ✅ WEBSOCKET COMMUNICATION
void sendDeviceOnline() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["serialNumber"] = DEVICE_SERIAL;
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["door_type"] = "ROLLING";
  doc["connection_type"] = "direct";
  doc["door_id"] = DOOR_ID;
  doc["features"] = "RFID_ACCESS,MANUAL_BUTTONS,STEPPER_MOTOR,WEB_CONFIG";
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"device_online\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  
  Serial.println("[WS] Device online sent");
}

void sendCommandResponse(String action, bool success, String result) {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["success"] = success;
  doc["result"] = result;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["command"] = action;
  doc["door_state"] = getStateString();
  doc["current_rounds"] = currentRounds;
  doc["door_type"] = "ROLLING";
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"command_response\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  
  Serial.println("[WS] Command response sent");
}

void sendDoorStatus() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["door_state"] = getStateString();
  doc["current_rounds"] = currentRounds;
  doc["door_type"] = "ROLLING";
  doc["is_moving"] = isMoving;
  doc["online"] = true;
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"deviceStatus\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  
  lastStatusSend = millis();
}

void sendDoorConfig() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["config_type"] = "stepper_config";
  doc["open_rounds"] = doorConfig.openRounds;
  doc["closed_rounds"] = doorConfig.closedRounds;
  doc["motor_speed"] = doorConfig.motorSpeed;
  doc["steps_per_round"] = doorConfig.stepsPerRound;
  doc["door_type"] = doorConfig.doorType;
  doc["reversed"] = doorConfig.reversed;
  doc["max_rounds"] = doorConfig.maxRounds;
  doc["authorized_tags"] = totalAuthorizedTags;
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"config_response\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  
  Serial.println("[WS] Config response sent");
}

void sendConfigResponse(String configType, bool success, String result) {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["success"] = success;
  doc["result"] = result;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["config_type"] = configType;
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"config_response\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  
  Serial.println("[WS] Config response sent");
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

// ✅ EEPROM FUNCTIONS
void loadDoorConfig() {
  uint8_t initFlag;
  EEPROM.get(ADDR_INIT_FLAG, initFlag);
  
  if (initFlag == 0xAA) {
    EEPROM.get(ADDR_OPEN_ROUNDS, doorConfig.openRounds);
    EEPROM.get(ADDR_CLOSED_ROUNDS, doorConfig.closedRounds);
    EEPROM.get(ADDR_MOTOR_SPEED, doorConfig.motorSpeed);
    
    Serial.println("[CONFIG] Loaded from EEPROM");
  } else {
    // Default configuration
    doorConfig.openRounds = DEFAULT_OPEN_ROUNDS;
    doorConfig.closedRounds = DEFAULT_CLOSED_ROUNDS;
    doorConfig.motorSpeed = DEFAULT_MOTOR_SPEED;
    
    Serial.println("[CONFIG] Using defaults");
    saveDoorConfig();
  }
  
  // Set other defaults
  doorConfig.stepsPerRound = STEPS_PER_REVOLUTION;
  doorConfig.reversed = false;
  doorConfig.maxRounds = 10;
  doorConfig.doorType = "ROLLING";
  
  // Validate ranges
  if (doorConfig.openRounds < 1 || doorConfig.openRounds > 10) doorConfig.openRounds = DEFAULT_OPEN_ROUNDS;
  if (doorConfig.closedRounds < 0 || doorConfig.closedRounds > 10) doorConfig.closedRounds = DEFAULT_CLOSED_ROUNDS;
  if (doorConfig.motorSpeed < 5 || doorConfig.motorSpeed > 50) doorConfig.motorSpeed = DEFAULT_MOTOR_SPEED;
  
  Serial.println("[CONFIG] Open: " + String(doorConfig.openRounds) + " rounds");
  Serial.println("[CONFIG] Closed: " + String(doorConfig.closedRounds) + " rounds");
  Serial.println("[CONFIG] Speed: " + String(doorConfig.motorSpeed) + " RPM");
}

void saveDoorConfig() {
  EEPROM.put(ADDR_OPEN_ROUNDS, doorConfig.openRounds);
  EEPROM.put(ADDR_CLOSED_ROUNDS, doorConfig.closedRounds);
  EEPROM.put(ADDR_MOTOR_SPEED, doorConfig.motorSpeed);
  EEPROM.put(ADDR_INIT_FLAG, 0xAA);
  EEPROM.commit();
  
  Serial.println("[CONFIG] Saved to EEPROM");
}

void loadDoorState() {
  EEPROM.get(ADDR_DOOR_STATE, isDoorOpen);
  currentRounds = isDoorOpen ? doorConfig.openRounds : doorConfig.closedRounds;
  doorState = isDoorOpen ? DOOR_OPEN : DOOR_CLOSED;
  
  Serial.println("[STATE] Door is " + String(isDoorOpen ? "OPEN" : "CLOSED"));
}

void saveDoorState() {
  EEPROM.put(ADDR_DOOR_STATE, isDoorOpen);
  EEPROM.commit();
}

// ✅ MANUAL CONTROLS
void handleManualButtons() {
  if (isMoving) return;
  
  int cwReading = digitalRead(BUTTON_CW_PIN);
  int ccwReading = digitalRead(BUTTON_CCW_PIN);

  // CW Button (Open)
  if (cwReading != lastCWButtonState) {
    lastCWDebounceTime = millis();
  }
  if ((millis() - lastCWDebounceTime) > debounceDelay) {
    if (cwReading != cwButtonState) {
      cwButtonState = cwReading;
      if (cwButtonState == LOW && !isDoorOpen) {
        Serial.println("[BUTTON] Manual open");
        openDoor();
      }
    }
  }
  lastCWButtonState = cwReading;

  // CCW Button (Close)
  if (ccwReading != lastCCWButtonState) {
    lastCCWDebounceTime = millis();
  }
  if ((millis() - lastCCWDebounceTime) > debounceDelay) {
    if (ccwReading != ccwButtonState) {
      ccwButtonState = ccwReading;
      if (ccwButtonState == LOW && isDoorOpen) {
        Serial.println("[BUTTON] Manual close");
        closeDoor();
      }
    }
  }
  lastCCWButtonState = ccwReading;
}

void handleRFID() {
  if (isMoving) return;
  
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String content = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
      content.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    content.toUpperCase();
    String cardID = content.substring(1);

    // Check authorized tags
    bool authorized = false;
    for (int i = 0; i < totalAuthorizedTags; i++) {
      if (cardID == authorizedTags[i]) {
        authorized = true;
        break;
      }
    }

    if (authorized) {
      Serial.println("[RFID] ✓ Authorized: " + cardID);
      if (!isDoorOpen) {
        openDoor();
      }
    } else {
      Serial.println("[RFID] ✗ Unauthorized: " + cardID);
    }
    
    mfrc522.PICC_HaltA();
  }
}

// ✅ CONNECTION CHECK
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
  
  if (!isMoving) {
    handleManualButtons();
    handleRFID();
  }
  
  // Periodic status updates
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 60000) {
    if (socketConnected) {
      sendDoorStatus();
    }
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
  
  // Status print
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 45000) {
    Serial.println("[STATUS] Connected: " + String(socketConnected ? "YES" : "NO") + 
                   " | Door: " + getStateString() + 
                   " | Rounds: " + String(currentRounds) +
                   " | Config Mode: Press GPIO0 during boot");
    lastStatusPrint = millis();
  }
  
  yield();
  delay(10);
}