// ========================================
// ESP8266 SLIDING DOOR - COMPLETE VERSION v5.0.2
// Direct WebSocket connection with WiFi UDP config + Web UI
// ========================================

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define FIRMWARE_VERSION "5.0.2"
#define DEVICE_TYPE "ESP8266_SLIDING_DOOR"
#define DOOR_ID 10

// ✅ DEVICE CONFIGURATION (can be updated via UDP/Web)
String DEVICE_SERIAL = "SERL27JUN2501JYR2RKVSLIDING001";
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
#define ADDR_PIR_CONFIG 20
#define ADDR_INIT_FLAG 24
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
const unsigned long CONFIG_TIMEOUT = 300000; // 5 minutes

// ✅ DOOR CONFIGURATION STRUCTURE
struct SlidingDoorConfig {
  int motorSpeed;           // PWM speed (0-255)
  unsigned long openDuration;     // Rotation time to open (ms)
  unsigned long closeDuration;    // Rotation time to close (ms)
  unsigned long waitBeforeClose;  // Wait time before auto close (ms)
  bool autoMode;           // Auto operation with PIR
  bool pir1Enabled;        // PIR1 sensor enabled
  bool pir2Enabled;        // PIR2 sensor enabled
  int pirSensitivity;      // PIR sensitivity (0-100)
  bool reversed;           // Reverse motor direction
  String doorType;         // "SLIDING"
};

SlidingDoorConfig doorConfig;

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

// ✅ WEBSOCKET STATUS
WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;
unsigned long lastStatusSend = 0;

// ✅ BUTTON HANDLING
bool lastButtonState = HIGH;
unsigned long lastButtonTime = 0;
const unsigned long buttonDebounce = 50;

// ✅ LED BUILTIN (ESP8266 LED is active LOW)
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP8266 SLIDING DOOR v5.0.2 ===");
  Serial.println("Device: " + DEVICE_SERIAL);
  Serial.println("Type: " + String(DEVICE_TYPE));
  Serial.println("Door ID: " + String(DOOR_ID));
  
  // ✅ INITIALIZE EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // ✅ INITIALIZE HARDWARE
  initializeHardware();
  
  // ✅ CHECK FOR CONFIG MODE (hold config button during boot)
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  delay(100);
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
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
  
  Serial.println("✓ Sliding Door Ready");
  Serial.println("Auto Mode: " + String(doorConfig.autoMode ? "ON" : "OFF"));
  Serial.println("================================\n");
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
  
  // LED for config mode indication (ESP8266 LED is active LOW)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn OFF (active LOW)
  
  // Stop motor initially
  stopMotor();
  
  Serial.println("[MOTOR] ✓ DC motor initialized");
  Serial.println("[PIR] ✓ Motion sensors initialized");
  Serial.println("[BUTTON] ✓ Manual button initialized");
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
    <title>ESP8266 Sliding Door Config</title>
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
            background: linear-gradient(135deg, #4CAF50, #45a049);
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
            outline: none; border-color: #4CAF50; 
            box-shadow: 0 0 0 3px rgba(76,175,80,0.1);
        }
        .btn { 
            width: 100%; padding: 15px; background: linear-gradient(135deg, #4CAF50, #45a049);
            color: white; border: none; border-radius: 8px; 
            font-size: 16px; font-weight: 600; cursor: pointer;
            transition: all 0.3s; margin-top: 10px;
        }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(76,175,80,0.3); }
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
            <h1>🚪 Sliding Door Config</h1>
            <p>ESP8266 WiFi Configuration</p>
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
            ESP8266 Sliding Door v5.0.2 | Device will restart after saving
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
  
  // Blink LED to indicate config mode (ESP8266 LED is active LOW)
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
          udp.write(response.c_str());
          udp.endPacket();
          
          Serial.println("[CONFIG] ✓ UDP Config saved, restarting...");
          delay(2000);
          ESP.restart();
        } else {
          // Send error response
          String response = "{\"success\":false,\"message\":\"Invalid SSID or password length\"}";
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(response.c_str());
          udp.endPacket();
        }
      } else {
        String response = "{\"success\":false,\"message\":\"Invalid JSON format\"}";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(response.c_str());
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
    
    // Turn off LED when connected (ESP8266 LED is active LOW)
    digitalWrite(LED_BUILTIN, HIGH);
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
  
  String userAgent = "ESP8266-Sliding-Door/5.0.2";
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
    
  } else if (action == "toggle_pir" || action == "PIR") {
    doorConfig.autoMode = !doorConfig.autoMode;
    saveDoorConfig();
    success = true;
    result = doorConfig.autoMode ? "auto_enabled" : "auto_disabled";
    
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
  
  if (configType == "motor_config") {
    int motorSpeed = doc["motor_speed"] | doorConfig.motorSpeed;
    unsigned long openDuration = doc["open_duration"] | doorConfig.openDuration;
    unsigned long closeDuration = doc["close_duration"] | doorConfig.closeDuration;
    
    if (motorSpeed >= 50 && motorSpeed <= 255 && 
        openDuration >= 500 && openDuration <= 10000 &&
        closeDuration >= 500 && closeDuration <= 10000) {
      
      doorConfig.motorSpeed = motorSpeed;
      doorConfig.openDuration = openDuration;
      doorConfig.closeDuration = closeDuration;
      
      saveDoorConfig();
      
      success = true;
      result = "motor_config_updated";
    } else {
      result = "invalid_motor_parameters";
    }
    
  } else if (configType == "sensor_config") {
    bool autoMode = doc["auto_mode"] | doorConfig.autoMode;
    bool pir1Enabled = doc["pir1_enabled"] | doorConfig.pir1Enabled;
    bool pir2Enabled = doc["pir2_enabled"] | doorConfig.pir2Enabled;
    unsigned long waitTime = doc["wait_before_close"] | doorConfig.waitBeforeClose;
    
    doorConfig.autoMode = autoMode;
    doorConfig.pir1Enabled = pir1Enabled;
    doorConfig.pir2Enabled = pir2Enabled;
    doorConfig.waitBeforeClose = waitTime;
    
    saveDoorConfig();
    
    success = true;
    result = "sensor_config_updated";
    
  } else if (configType == "get_config") {
    sendDoorConfig();
    return;
  }
  
  sendConfigResponse(configType, success, result);
}

// ✅ DOOR CONTROL FUNCTIONS
bool openDoor() {
  if (isMoving) return false;
  
  if (isDoorOpen) {
    Serial.println("[DOOR] Already open");
    return false;
  }
  
  isMoving = true;
  doorState = DOOR_OPENING;
  motorStartTime = millis();
  
  Serial.println("[DOOR] Opening - Duration: " + String(doorConfig.openDuration) + "ms");
  
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
  if (isMoving) return false;
  
  if (!isDoorOpen) {
    Serial.println("[DOOR] Already closed");
    return false;
  }
  
  isMoving = true;
  doorState = DOOR_CLOSING;
  motorStartTime = millis();
  
  Serial.println("[DOOR] Closing - Duration: " + String(doorConfig.closeDuration) + "ms");
  
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

// ✅ HANDLE DOOR MOVEMENT
void handleDoorMovement() {
  if (!isMoving) return;
  
  unsigned long elapsedTime = millis() - motorStartTime;
  unsigned long targetDuration;
  
  if (doorState == DOOR_OPENING) {
    targetDuration = doorConfig.openDuration;
    
    // Check for motion during opening
    if (doorConfig.autoMode && (readPIR1() || readPIR2())) {
      lastMotionTime = millis();
    }
    
    if (elapsedTime >= targetDuration) {
      stopMotor();
      isDoorOpen = true;
      doorState = DOOR_OPEN;
      isMoving = false;
      lastMotionTime = millis();
      
      saveDoorState();
      sendDoorStatus();
      
      Serial.println("[DOOR] ✓ Opened");
    }
    
  } else if (doorState == DOOR_CLOSING) {
    targetDuration = doorConfig.closeDuration;
    
    // Check for motion during closing (safety)
    if (doorConfig.autoMode && (readPIR1() || readPIR2())) {
      Serial.println("[SAFETY] Motion detected during closing, reopening...");
      
      // Calculate how long we've been closing
      unsigned long closingTime = elapsedTime;
      stopMotor();
      
      // Reverse to open position
      delay(100);
      analogWrite(ENA_PIN, doorConfig.motorSpeed);
      if (!doorConfig.reversed) {
        digitalWrite(MOTOR_PIN1, HIGH);
        digitalWrite(MOTOR_PIN2, LOW);
      } else {
        digitalWrite(MOTOR_PIN1, LOW);
        digitalWrite(MOTOR_PIN2, HIGH);
      }
      
      // Run for the same duration we were closing
      delay(closingTime);
      stopMotor();
      
      isDoorOpen = true;
      doorState = DOOR_OPEN;
      isMoving = false;
      lastMotionTime = millis();
      
      Serial.println("[SAFETY] ✓ Reopened due to motion");
      sendDoorStatus();
      return;
    }
    
    if (elapsedTime >= targetDuration) {
      stopMotor();
      isDoorOpen = false;
      doorState = DOOR_CLOSED;
      isMoving = false;
      
      saveDoorState();
      sendDoorStatus();
      
      Serial.println("[DOOR] ✓ Closed");
    }
  }
}

// ✅ PIR SENSOR FUNCTIONS
bool readPIR1() {
  return doorConfig.pir1Enabled && digitalRead(PIR1_PIN) == HIGH;
}

bool readPIR2() {
  return doorConfig.pir2Enabled && digitalRead(PIR2_PIN) == HIGH;
}

void handleAutoOperation() {
  if (!doorConfig.autoMode || isMoving) return;
  
  bool motionDetected = readPIR1() || readPIR2();
  
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

// ✅ MANUAL BUTTON
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

// ✅ WEBSOCKET COMMUNICATION
void sendDeviceOnline() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = DEVICE_SERIAL;
  doc["serialNumber"] = DEVICE_SERIAL;
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["door_type"] = "SLIDING";
  doc["connection_type"] = "direct";
  doc["door_id"] = DOOR_ID;
  doc["features"] = "PIR_SENSORS,AUTO_MODE,MANUAL_OVERRIDE,WEB_CONFIG";
  
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
  doc["door_type"] = "SLIDING";
  doc["auto_mode"] = doorConfig.autoMode;
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
  doc["door_type"] = "SLIDING";
  doc["is_moving"] = isMoving;
  doc["auto_mode"] = doorConfig.autoMode;
  doc["pir1_state"] = readPIR1();
  doc["pir2_state"] = readPIR2();
  doc["motor_speed"] = doorConfig.motorSpeed;
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
  doc["config_type"] = "sliding_config";
  doc["motor_speed"] = doorConfig.motorSpeed;
  doc["open_duration"] = doorConfig.openDuration;
  doc["close_duration"] = doorConfig.closeDuration;
  doc["wait_before_close"] = doorConfig.waitBeforeClose;
  doc["auto_mode"] = doorConfig.autoMode;
  doc["pir1_enabled"] = doorConfig.pir1Enabled;
  doc["pir2_enabled"] = doorConfig.pir2Enabled;
  doc["door_type"] = doorConfig.doorType;
  doc["reversed"] = doorConfig.reversed;
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
  
  if (initFlag == 0xCC) {
    EEPROM.get(ADDR_MOTOR_SPEED, doorConfig.motorSpeed);
    EEPROM.get(ADDR_OPEN_DURATION, doorConfig.openDuration);
    EEPROM.get(ADDR_WAIT_TIME, doorConfig.waitBeforeClose);
    EEPROM.get(ADDR_AUTO_MODE, doorConfig.autoMode);
    EEPROM.get(ADDR_PIR_CONFIG, doorConfig.pir1Enabled);
    
    Serial.println("[CONFIG] Loaded from EEPROM");
  } else {
    // Default configuration
    doorConfig.motorSpeed = 80;              // PWM 0-255
    doorConfig.openDuration = 2000;          // 2 seconds
    doorConfig.closeDuration = 2000;         // 2 seconds  
    doorConfig.waitBeforeClose = 4000;       // 4 seconds wait
    doorConfig.autoMode = true;              // Auto mode enabled
    doorConfig.pir1Enabled = true;           // PIR1 enabled
    doorConfig.pir2Enabled = true;           // PIR2 enabled
    
    Serial.println("[CONFIG] Using defaults");
    saveDoorConfig();
  }
  
  // Set other defaults
  doorConfig.pirSensitivity = 80;
  doorConfig.reversed = false;
  doorConfig.doorType = "SLIDING";
  
  // Validate ranges
  if (doorConfig.motorSpeed < 50 || doorConfig.motorSpeed > 255) doorConfig.motorSpeed = 80;
  if (doorConfig.openDuration < 500 || doorConfig.openDuration > 10000) doorConfig.openDuration = 2000;
  if (doorConfig.closeDuration < 500 || doorConfig.closeDuration > 10000) doorConfig.closeDuration = 2000;
  if (doorConfig.waitBeforeClose < 1000 || doorConfig.waitBeforeClose > 30000) doorConfig.waitBeforeClose = 4000;
  
  Serial.println("[CONFIG] Motor Speed: " + String(doorConfig.motorSpeed));
  Serial.println("[CONFIG] Open Duration: " + String(doorConfig.openDuration) + "ms");
  Serial.println("[CONFIG] Wait Time: " + String(doorConfig.waitBeforeClose) + "ms");
  Serial.println("[CONFIG] Auto Mode: " + String(doorConfig.autoMode ? "ON" : "OFF"));
}

void saveDoorConfig() {
  EEPROM.put(ADDR_MOTOR_SPEED, doorConfig.motorSpeed);
  EEPROM.put(ADDR_OPEN_DURATION, doorConfig.openDuration);
  EEPROM.put(ADDR_WAIT_TIME, doorConfig.waitBeforeClose);
  EEPROM.put(ADDR_AUTO_MODE, doorConfig.autoMode);
  EEPROM.put(ADDR_PIR_CONFIG, doorConfig.pir1Enabled);
  EEPROM.put(ADDR_INIT_FLAG, 0xCC);
  EEPROM.commit();
  
  Serial.println("[CONFIG] Saved to EEPROM");
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
  
  // Handle door movement
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
                   " | Auto: " + String(doorConfig.autoMode ? "ON" : "OFF") +
                   " | PIR1: " + String(readPIR1() ? "MOTION" : "CLEAR") +
                   " | PIR2: " + String(readPIR2() ? "MOTION" : "CLEAR") +
                   " | Config Mode: Press GPIO16 during boot");
    lastStatusPrint = millis();
  }
  
  yield();
  delay(10);
}