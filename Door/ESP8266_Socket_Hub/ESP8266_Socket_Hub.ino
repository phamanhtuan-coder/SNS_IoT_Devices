// ========================================
// ESP8266 SOCKET HUB v5.0.0 - GARDEN CONTROL ONLY
// ========================================

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// ✅ DEVICE CONFIGURATION (can be updated via UDP/Web)
String WIFI_SSID = "Anh Tuan";
String WIFI_PASSWORD = "21032001";
String WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
uint16_t WEBSOCKET_PORT = 443;

String HUB_SERIAL = "SERL29JUN2501JYXECBR32V8BD77RW82";
#define FIRMWARE_VERSION "5.0.0"
#define HUB_ID "ESP_HUB_GARDEN_001"

// ✅ EEPROM ADDRESSES
#define EEPROM_SIZE 512
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

// ✅ GARDEN MANAGED DEVICES (Hard-coded serials for demo)
String managedDevices[] = {
  "MEGA27JUN2501GARDEN_HUB_001",           // Arduino Mega Garden Hub
  "RELAY27JUN2501FAN001CONTROL001",        // Fan Relay
  "RELAY27JUN2501ALARM01CONTROL01",        // Alarm Relay
  "RELAY27JUN2501LIGHT001CONTROL1",        // Light1 Relay
  "RELAY27JUN2501LIGHT002CONTROL1",        // Light2 Relay
  "RELAY27JUN2501PUMP002CONTROL01",        // Pump2 Relay
  "RELAY27JUN2501HEATER1CONTROL01",        // Heater Relay
  "RELAY27JUN2501COOLER1CONTROL01",        // Cooler Relay
  "RELAY27JUN2501RESERVE8CONTROL1"         // Reserve Relay
};
const int TOTAL_MANAGED_DEVICES = 9;

WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;
bool devicesRegistered = false;

// ✅ GARDEN COMMUNICATION
bool gardenHubConnected = false;
unsigned long lastGardenMessage = 0;
const unsigned long GARDEN_TIMEOUT = 120000; // 2 minutes

// ✅ LED BUILTIN (ESP8266 LED is active LOW)
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  Serial.println("\n=== ESP Socket Hub v5.0.0 (GARDEN CONTROL ONLY) ===");
  Serial.println("Hub: " + HUB_SERIAL);
  Serial.println("ID: " + String(HUB_ID));
  Serial.println("Garden System: Arduino Mega Garden Hub + 8 Relays");
  Serial.println("Managing " + String(TOTAL_MANAGED_DEVICES) + " garden devices");
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()));
  
  // ✅ INITIALIZE EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // ✅ INITIALIZE LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn OFF (active LOW)
  
  // ✅ CHECK FOR CONFIG MODE (hold GPIO0 during boot)
  pinMode(0, INPUT_PULLUP);
  delay(100);
  if (digitalRead(0) == LOW) {
    Serial.println("[CONFIG] Config button pressed, entering WiFi config mode...");
    startConfigMode();
  } else {
    // ✅ NORMAL OPERATION MODE
    if (loadWiFiConfig()) {
      Serial.println("[CONFIG] Using saved WiFi config");
    } else {
      Serial.println("[CONFIG] Using default WiFi config");
    }
    
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      setupWebSocket();
    }
  }
  
  Serial.println("Garden Hub Ready - Connected to Arduino Mega");
  Serial.println("Config Mode: Hold GPIO0 during boot");
  Serial.println("===============================================\n");
}

// ===== WIFI CONFIG MODE FUNCTIONS =====
void startConfigMode() {
  configMode = true;
  configModeStart = millis();
  
  Serial.println("[CONFIG] Starting WiFi AP for configuration...");
  
  // Create AP
  String apName = "ESP_GARDEN_" + String(HUB_ID);
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
  webServer.on("/", handleConfigPage);
  webServer.on("/save", HTTP_POST, handleSaveConfig);
  webServer.on("/status", HTTP_GET, handleStatus);
  webServer.onNotFound(handleNotFound);
}

void handleConfigPage() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP Garden Hub Config</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #4CAF50 0%, #81C784 100%);
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
            background: linear-gradient(135deg, #4CAF50, #66BB6A);
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
            width: 100%; padding: 15px; background: linear-gradient(135deg, #4CAF50, #66BB6A);
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
            background: #e8f5e8; color: #2e7d32; border: 1px solid #c8e6c9;
            margin-bottom: 20px; padding: 15px; border-radius: 8px;
        }
        .stats {
            background: #f8f9fa; padding: 15px; border-radius: 8px;
            margin-bottom: 20px; font-size: 14px;
        }
        .footer { 
            background: #f8f9fa; padding: 20px; text-align: center; 
            color: #666; font-size: 12px; border-top: 1px solid #e9ecef;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🌱 Garden Hub Config</h1>
            <p>ESP8266 Garden Control System</p>
        </div>
        
        <div class="form">
            <div class="stats">
                <strong>🌿 Garden Hub Information:</strong><br>
                Serial: )" + HUB_SERIAL + R"(<br>
                Version: )" + String(FIRMWARE_VERSION) + R"(<br>
                Garden Devices: )" + String(TOTAL_MANAGED_DEVICES) + R"(<br>
                Free Heap: )" + String(ESP.getFreeHeap()) + R"( bytes
            </div>
            
            <div class="info">
                <strong>🌱 Garden System Features:</strong><br>
                1. Arduino Mega Garden Hub Control<br>
                2. 8-Channel Relay Control (Fan, Lights, Pump, etc.)<br>
                3. Garden Sensor Monitoring<br>
                4. RGB LED Status Indication<br>
                5. LCD & OLED Display Support
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
                    <label for="serial">🏷️ Hub Serial (Optional)</label>
                    <input type="text" id="serial" name="serial" maxlength="31"
                           placeholder="Leave empty to keep current">
                </div>
                
                <button type="submit" class="btn">🌱 Save Garden Config</button>
            </form>
        </div>
        
        <div class="footer">
            ESP Garden Hub v5.0.0 | Connected to Arduino Mega
        </div>
    </div>

    <script>
        document.getElementById('configForm').addEventListener('submit', async function(e) {
            e.preventDefault();
            
            const statusDiv = document.getElementById('status');
            const formData = new FormData(this);
            const submitBtn = this.querySelector('button[type="submit"]');
            
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
            
            submitBtn.disabled = true;
            submitBtn.textContent = '⏳ Saving...';
            statusDiv.innerHTML = '<div class="status">⏳ Saving garden hub configuration...</div>';
            
            try {
                const response = await fetch('/save', {
                    method: 'POST',
                    body: formData
                });
                
                const result = await response.json();
                
                if (result.success) {
                    statusDiv.innerHTML = '<div class="status success">✅ ' + result.message + '</div>';
                    setTimeout(() => {
                        statusDiv.innerHTML = '<div class="status">🔄 Garden Hub restarting... Please reconnect to your home WiFi.</div>';
                    }, 2000);
                } else {
                    statusDiv.innerHTML = '<div class="status error">❌ ' + result.message + '</div>';
                    submitBtn.disabled = false;
                    submitBtn.textContent = '🌱 Save Garden Config';
                }
            } catch (error) {
                statusDiv.innerHTML = '<div class="status error">❌ Connection error: ' + error.message + '</div>';
                submitBtn.disabled = false;
                submitBtn.textContent = '🌱 Save Garden Config';
            }
        });
        
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
    
    if (newSSID.length() > 0 && newSSID.length() <= 31 && 
        newPassword.length() > 0 && newPassword.length() <= 31) {
      
      if (newSerial.length() > 0 && newSerial.length() <= 31) {
        HUB_SERIAL = newSerial;
      }
      
      saveWiFiConfig(newSSID, newPassword, newSerial);
      
      success = true;
      response = "{\"success\":true,\"message\":\"Garden hub configuration saved! Restarting in 3 seconds.\"}";
      
      Serial.println("[CONFIG] ✓ Garden hub config saved");
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
  doc["device_type"] = "ESP_GARDEN_HUB";
  doc["hub_serial"] = HUB_SERIAL;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["managed_devices"] = TOTAL_MANAGED_DEVICES;
  doc["garden_connected"] = gardenHubConnected;
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  doc["socket_connected"] = socketConnected;
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void handleNotFound() {
  webServer.send(404, "text/plain", "Not found - Please go to http://" + WiFi.softAPIP().toString());
}

void handleConfigMode() {
  if (!configMode) return;
  
  if (millis() - configModeStart > CONFIG_TIMEOUT) {
    Serial.println("[CONFIG] Timeout, restarting...");
    ESP.restart();
  }
  
  // Blink LED to indicate config mode
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
    Serial.println("[CONFIG] Received UDP packet: " + String(packetSize) + " bytes");
    
    char packet[256];
    int len = udp.read(packet, 255);
    if (len > 0) {
      packet[len] = 0;
      
      String message = String(packet);
      Serial.println("[CONFIG] UDP Message: " + message);
      
      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, message) == DeserializationError::Ok) {
        String newSSID = doc["ssid"].as<String>();
        String newPassword = doc["password"].as<String>();
        String newSerial = doc["serial"].as<String>();
        
        if (newSSID.length() > 0 && newSSID.length() <= 31 && 
            newPassword.length() > 0 && newPassword.length() <= 31) {
          
          if (newSerial.length() > 0 && newSerial.length() <= 31) {
            HUB_SERIAL = newSerial;
          }
          
          saveWiFiConfig(newSSID, newPassword, newSerial);
          
          String response = "{\"success\":true,\"message\":\"Garden hub config saved, restarting...\"}";
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(response.c_str());
          udp.endPacket();
          
          Serial.println("[CONFIG] ✓ UDP Garden config saved, restarting...");
          delay(2000);
          ESP.restart();
        } else {
          String response = "{\"success\":false,\"message\":\"Invalid SSID or password length\"}";
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(response.c_str());
          udp.endPacket();
        }
      }
    }
  }
}

void saveWiFiConfig(String ssid, String password, String serial) {
  if (ssid.length() > 31) ssid = ssid.substring(0, 31);
  if (password.length() > 31) password = password.substring(0, 31);
  if (serial.length() > 31) serial = serial.substring(0, 31);
  
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, i < ssid.length() ? ssid[i] : 0);
  }
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_PASS_ADDR + i, i < password.length() ? password[i] : 0);
  }
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_SERIAL_ADDR + i, i < serial.length() ? serial[i] : 0);
  }
  EEPROM.write(WIFI_CONFIG_FLAG_ADDR, 0xAB);
  EEPROM.commit();
  
  Serial.println("[CONFIG] Garden hub config saved to EEPROM");
}

bool loadWiFiConfig() {
  if (EEPROM.read(WIFI_CONFIG_FLAG_ADDR) != 0xAB) {
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
  
  WIFI_SSID = String(ssid);
  WIFI_PASSWORD = String(password);
  
  if (strlen(serial) > 0) {
    HUB_SERIAL = String(serial);
  }
  
  Serial.println("[CONFIG] ✓ WiFi config loaded from EEPROM");
  Serial.println("[CONFIG] SSID: " + WIFI_SSID);
  Serial.println("[CONFIG] Serial: " + HUB_SERIAL);
  
  return true;
}

void setupWiFi() {
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
    digitalWrite(LED_BUILTIN, HIGH); // Turn OFF (connected)
  } else {
    Serial.println(" ✗ FAILED");
    Serial.println("[WiFi] Starting config mode...");
    startConfigMode();
  }
}

void setupWebSocket() {
  String path = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + 
                HUB_SERIAL + "&isIoTDevice=true&hub_managed=true&optimized=true&garden_hub=true";
  
  Serial.println("[WS] Connecting to " + WEBSOCKET_HOST + ":" + String(WEBSOCKET_PORT));
  
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(25000, 5000, 2);
  
  String userAgent = "ESP-Garden-Hub/5.0.0";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[WS] Garden hub setup complete");
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] ✗ DISCONNECTED");
      socketConnected = false;
      devicesRegistered = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WS] ✓ CONNECTED Garden Hub:" + HUB_SERIAL);
      socketConnected = true;
      lastPingResponse = millis();
      devicesRegistered = false;
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

void registerManagedDevices() {
  if (devicesRegistered || !socketConnected) return;
  
  Serial.println("[REGISTER] Registering " + String(TOTAL_MANAGED_DEVICES) + " garden devices...");
  
  // Register Arduino Mega Garden Hub
  StaticJsonDocument<512> megaDoc;
  megaDoc["deviceId"] = "MEGA27JUN2501GARDEN_HUB_001";
  megaDoc["serialNumber"] = "MEGA27JUN2501GARDEN_HUB_001";
  megaDoc["deviceType"] = "MEGA_GARDEN_HUB";
  megaDoc["hub_controlled"] = true;
  megaDoc["hub_serial"] = HUB_SERIAL;
  megaDoc["connection_type"] = "serial_via_garden_hub";
  megaDoc["firmware_version"] = "3.0.0";
  megaDoc["garden_system"] = true;

  String megaPayload;
  serializeJson(megaDoc, megaPayload);
  String megaFullPayload = "42[\"device_online\"," + megaPayload + "]";
  webSocket.sendTXT(megaFullPayload);
  
  Serial.println("[REGISTER] Arduino Mega Garden Hub: MEGA27JUN2501GARDEN_HUB_001");
  delay(100);
  
  // Register 8 Relay devices
  for (int i = 1; i < TOTAL_MANAGED_DEVICES; i++) {
    StaticJsonDocument<512> doc;
    doc["deviceId"] = managedDevices[i];
    doc["serialNumber"] = managedDevices[i];
    doc["deviceType"] = "GARDEN_RELAY_DEVICE";
    doc["hub_controlled"] = true;
    doc["hub_serial"] = HUB_SERIAL;
    doc["connection_type"] = "relay_via_garden_hub";
    doc["relay_id"] = i;
    doc["firmware_version"] = "3.0.0";
    doc["garden_device"] = true;

    String payload;
    serializeJson(doc, payload);
    String fullPayload = "42[\"device_online\"," + payload + "]";
    webSocket.sendTXT(fullPayload);
    
    Serial.println("[REGISTER] Relay " + String(i) + ": " + managedDevices[i]);
    delay(100);
  }
  
  devicesRegistered = true;
  Serial.println("[REGISTER] ✅ Garden devices registration complete");
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char type = message.charAt(0);
  
  if (type == '0') {
    if (!devicesRegistered) {
      delay(1000);
      registerManagedDevices();
    }
    
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
  
  if (type == '0') {
    if (!devicesRegistered) {
      delay(1000);
      registerManagedDevices();
    }
    
  } else if (type == '2') {
    String eventData = data.substring(1);
    handleSocketIOEvent(eventData);
  }
}

void handleSocketIOEvent(String eventData) {
  if (eventData.indexOf("command") != -1) {
    parseAndExecuteCommand(eventData);
    
  } else if (eventData.indexOf("ping") != -1) {
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"hub_serial\":\"" + HUB_SERIAL + "\",\"garden_hub\":true}]";
    webSocket.sendTXT(pongPayload);
    lastPingResponse = millis();
  }
}

void parseAndExecuteCommand(String eventData) {
  int startIdx = eventData.indexOf("{");
  int endIdx = eventData.lastIndexOf("}");
  
  if (startIdx == -1 || endIdx == -1) return;
  
  String jsonString = eventData.substring(startIdx, endIdx + 1);
  JsonDocument doc;
  if (deserializeJson(doc, jsonString) != DeserializationError::Ok) return;
  
  // ✅ GARDEN COMMAND PROCESSING
  String action = doc["action"].is<String>() ? doc["action"].as<String>() : "";
  String command = doc["command"].is<String>() ? doc["command"].as<String>() : "";
  String target = doc["target"].is<String>() ? doc["target"].as<String>() : "";
  
  Serial.println("[GARDEN-CMD] Action: " + action + " | Target: " + target + " | Command: " + command);
  
  // ✅ GARDEN PUMP COMMANDS
  if (action == "garden_command" || action == "garden_pump" || target.startsWith("mega_")) {
    if (command.length() > 0) {
      Serial.println("[GARDEN] → Mega: " + command);
      Serial.println(command); // Send to Arduino Mega via Serial
    } else if (action == "garden_pump") {
      String pumpAction = doc["pump_action"].as<String>();
      String cmd = "CMD:GARDEN:PUMP_" + pumpAction;
      Serial.println("[GARDEN] → Mega: " + cmd);
      Serial.println(cmd);
    }
    return;
  }
  
  // ✅ GARDEN RELAY COMMANDS
  if (action == "relay_command" || target == "mega_relay") {
    if (command.length() > 0) {
      Serial.println("[RELAY] → Mega: " + command);
      Serial.println(command); // Send to Arduino Mega via Serial
    }
    return;
  }
  
  // ✅ GARDEN RGB COMMANDS
  if (action == "garden_rgb" || target == "mega_garden") {
    String rgbAction = doc["rgb_action"].as<String>();
    String cmd = "CMD:GARDEN:RGB_" + rgbAction;
    Serial.println("[RGB] → Mega: " + cmd);
    Serial.println(cmd);
    return;
  }
  
  // ✅ GARDEN AUTOMATION COMMANDS
  if (action == "garden_automation") {
    String automationType = doc["automation_type"].as<String>();
    String cmd = "CMD:GARDEN:AUTO_" + automationType + "_TOGGLE";
    Serial.println("[AUTO] → Mega: " + cmd);
    Serial.println(cmd);
    return;
  }
  
  // ✅ GARDEN THRESHOLD COMMANDS
  if (action == "garden_threshold") {
    String thresholdType = doc["threshold_type"].as<String>();
    int value = doc["value"].as<int>();
    String cmd = "CMD:GARDEN:SET_" + thresholdType + "_THRESHOLD:" + String(value);
    Serial.println("[THRESHOLD] → Mega: " + cmd);
    Serial.println(cmd);
    return;
  }
  
  // ✅ EMERGENCY ALARM COMMANDS
  if (action == "emergency_alarm" || target == "mega_alarm") {
    String alarmAction = doc["alarm_action"].as<String>();
    String relaySerial = doc["relay_serial"].as<String>();
    String cmd = "CMD:" + relaySerial + ":" + (alarmAction == "ACTIVATE" ? "ON" : 
                alarmAction == "DEACTIVATE" ? "OFF" : "RESET_OVERRIDE");
    Serial.println("[ALARM] → Mega: " + cmd);
    Serial.println(cmd);
    return;
  }
}

void sendDeviceOnline() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = HUB_SERIAL;
  doc["serialNumber"] = HUB_SERIAL;
  doc["deviceType"] = "GARDEN_HUB_GATEWAY";
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["hub_managed"] = true;
  doc["hub_id"] = HUB_ID;
  doc["connection_type"] = "garden_hub_optimized";
  doc["managed_devices_count"] = TOTAL_MANAGED_DEVICES;
  doc["garden_support"] = true;
  doc["garden_only"] = true;

  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"device_online\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  Serial.println("[ONLINE] Garden Hub registered with " + String(TOTAL_MANAGED_DEVICES) + " devices");
}

void checkConnectionStatus() {
  static unsigned long lastReconnectAttempt = 0;
  static int reconnectAttempts = 0;
  const int MAX_RECONNECT_ATTEMPTS = 5;
  const unsigned long RECONNECT_INTERVAL = 30000;
  
  if (!socketConnected) {
    unsigned long now = millis();
    
    if (now - lastReconnectAttempt < RECONNECT_INTERVAL) {
      return;
    }
    
    if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
      Serial.println("[RECONNECT] Max attempts reached, restarting...");
      delay(1000);
      ESP.restart();
    }
    
    Serial.println("[RECONNECT] Garden hub attempt " + String(reconnectAttempts + 1));
    
    webSocket.disconnect();
    delay(2000);
    
    if (reconnectAttempts >= 2) {
      WiFi.disconnect();
      delay(1000);
      setupWiFi();
    }
    
    setupWebSocket();
    lastReconnectAttempt = now;
    reconnectAttempts++;
  } else {
    reconnectAttempts = 0;
  }
}

void checkGardenConnection() {
  if (gardenHubConnected && millis() - lastGardenMessage > GARDEN_TIMEOUT) {
    gardenHubConnected = false;
    Serial.println("[GARDEN] ✗ Arduino Mega timeout - connection lost");
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
  checkConnectionStatus();
  checkGardenConnection();
  
  // Manual ping
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 30000) {
    webSocket.sendTXT("2");
    lastManualPing = millis();
  }

  // ✅ HANDLE ARDUINO MEGA GARDEN RESPONSES
  if (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    response.trim();
    
    if (response.length() == 0) return;
    
    // Update garden connection status
    gardenHubConnected = true;
    lastGardenMessage = millis();
    
    // Filter out debug messages to prevent flooding
    if (response.startsWith("[") || 
        response.indexOf("DEBUG") != -1 ||
        response.indexOf("INIT") != -1 ||
        response.startsWith("===")) {
      return;
    }
    
    Serial.println("[MEGA-RX] " + response);
    
    // ✅ GARDEN DATA FORWARDING TO SERVER
    if (response.startsWith("GARDEN_DATA:")) {
      String json = response.substring(12);
      String payload = "42[\"garden_sensor_data\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Garden sensor data forwarded");
      
    } else if (response.startsWith("GARDEN_STATUS:")) {
      String json = response.substring(14);
      String payload = "42[\"garden_automation_status\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Garden automation status forwarded");
      
    } else if (response.startsWith("PUMP_RESPONSE:")) {
      String json = response.substring(14);
      String payload = "42[\"pump_response\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Pump response forwarded");
      
    } else if (response.startsWith("RGB_STATUS:")) {
      String json = response.substring(11);
      String payload = "42[\"rgb_status_update\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] RGB status forwarded");
      
    } else if (response.startsWith("RELAY_STATUS:")) {
      String json = response.substring(13);
      String payload = "42[\"relay_status_update\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Relay status forwarded");
      
    } else if (response.startsWith("GARDEN_AUTOMATION:")) {
      String json = response.substring(18);
      String payload = "42[\"garden_automation_status\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Garden automation forwarded");
      
    } else if (response.startsWith("GARDEN_HEALTH:")) {
      String json = response.substring(14);
      String payload = "42[\"garden_system_health\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Garden health forwarded");
      
    } else if (response.startsWith("RESP:")) {
      String json = response.substring(5);
      String payload = "42[\"command_response\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Command response forwarded");
      
    } else if (response.startsWith("STS:")) {
      String json = response.substring(4);
      String payload = "42[\"deviceStatus\"," + json + "]";
      webSocket.sendTXT(payload);
      Serial.println("[→SERVER] Device status forwarded");
    }
  }
  
  // Status print
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    Serial.println("[STATUS] Garden Hub - Socket:" + String(socketConnected) + 
                   " | Garden:" + String(gardenHubConnected) +
                   " | Heap:" + String(ESP.getFreeHeap()) + 
                   " | Uptime:" + String(millis() / 1000) + "s" +
                   " | Devices:" + String(TOTAL_MANAGED_DEVICES));
    lastStatus = millis();
  }
  
  yield();
  delay(10);
}