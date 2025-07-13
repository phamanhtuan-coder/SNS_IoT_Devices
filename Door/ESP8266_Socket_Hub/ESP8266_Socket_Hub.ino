// ========================================
// ESP8266 SOCKET HUB v4.2.0 - WITH UDP CONFIG + GARDEN SUPPORT
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
#define FIRMWARE_VERSION "4.2.0"
#define HUB_ID "ESP_HUB_OPT_001"

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

// ✅ MANAGED DEVICES
String managedDevices[] = {
  "SERL27JUN2501JYR2RKVVX08V40YMGTW",  // Door 1
  "SERL27JUN2501JYR2RKVR0SC7SJ8P8DD",  // Door 2
  "SERL27JUN2501JYR2RKVRNHS46VR6AS1",  // Door 3
  "SERL27JUN2501JYR2RKVSE2RW7KQ4KMP",  // Door 4
  "SERL27JUN2501JYR2RKVTBZ40JPF88WP",  // Door 5
  "SERL27JUN2501JYR2RKVTXNCK1GB3HBZ",  // Door 6
  "SERL27JUN2501JYR2RKVS2P6XBVF1P2E",  // Door 7
  "SERL27JUN2501JYR2RKVTH6PWR9ETXC2",  // Door 8
  "SERL27JUN2501JYR2RKVVSBGRTM0TRFW",  // Door 9
  "MEGA27JUN2501GARDEN_HUB_001"        // Garden Hub
};
const int TOTAL_MANAGED_DEVICES = 10;

WebSocketsClient webSocket;
bool socketConnected = false;
unsigned long lastPingResponse = 0;
bool devicesRegistered = false;

// ✅ LED BUILTIN (ESP8266 LED is active LOW)
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  Serial.println("\n=== ESP Socket Hub v4.2.0 (UDP CONFIG + GARDEN) ===");
  Serial.println("Hub: " + HUB_SERIAL);
  Serial.println("ID: " + String(HUB_ID));
  Serial.println("Managing " + String(TOTAL_MANAGED_DEVICES) + " devices");
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
  
  Serial.println("Hub Ready - Enhanced Stability + Garden Support");
  Serial.println("Config Mode: Hold GPIO0 during boot");
  Serial.println("===============================================\n");
}

// ===== WIFI CONFIG MODE FUNCTIONS =====
void startConfigMode() {
  configMode = true;
  configModeStart = millis();
  
  Serial.println("[CONFIG] Starting WiFi AP for configuration...");
  
  // Create AP
  String apName = "ESP_HUB_" + String(HUB_ID);
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
    <title>ESP Socket Hub Config</title>
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
            background: linear-gradient(135deg, #FF6B35, #F7931E);
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
            outline: none; border-color: #FF6B35; 
            box-shadow: 0 0 0 3px rgba(255,107,53,0.1);
        }
        .btn { 
            width: 100%; padding: 15px; background: linear-gradient(135deg, #FF6B35, #F7931E);
            color: white; border: none; border-radius: 8px; 
            font-size: 16px; font-weight: 600; cursor: pointer;
            transition: all 0.3s; margin-top: 10px;
        }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(255,107,53,0.3); }
        .status { 
            padding: 15px; margin: 15px 0; border-radius: 8px; 
            text-align: center; font-weight: 600;
        }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .info { 
            background: #fff3cd; color: #856404; border: 1px solid #ffeaa7;
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
            <h1>🌐 Socket Hub Config</h1>
            <p>ESP8266 WiFi Configuration</p>
        </div>
        
        <div class="form">
            <div class="stats">
                <strong>📊 Hub Information:</strong><br>
                Serial: )" + HUB_SERIAL + R"(<br>
                Version: )" + String(FIRMWARE_VERSION) + R"(<br>
                Managed Devices: )" + String(TOTAL_MANAGED_DEVICES) + R"(<br>
                Free Heap: )" + String(ESP.getFreeHeap()) + R"( bytes
            </div>
            
            <div class="info">
                <strong>📋 Instructions:</strong><br>
                1. Connect to this WiFi network<br>
                2. Enter your home WiFi credentials<br>
                3. Optionally update hub serial<br>
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
                    <label for="serial">🏷️ Hub Serial (Optional)</label>
                    <input type="text" id="serial" name="serial" maxlength="31"
                           placeholder="Leave empty to keep current">
                </div>
                
                <button type="submit" class="btn">💾 Save Configuration</button>
            </form>
        </div>
        
        <div class="footer">
            ESP Socket Hub v4.2.0 | Hub will restart after saving
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
                        statusDiv.innerHTML = '<div class="status">🔄 Hub is restarting... Please reconnect to your home WiFi.</div>';
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
      response = "{\"success\":true,\"message\":\"Configuration saved successfully! Hub will restart in 3 seconds.\"}";
      
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
  doc["device_type"] = "ESP_SOCKET_HUB";
  doc["hub_serial"] = HUB_SERIAL;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["managed_devices"] = TOTAL_MANAGED_DEVICES;
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
          
          String response = "{\"success\":true,\"message\":\"WiFi config saved, restarting...\"}";
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(response.c_str());
          udp.endPacket();
          
          Serial.println("[CONFIG] ✓ UDP Config saved, restarting...");
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
  
  Serial.println("[CONFIG] WiFi config saved to EEPROM");
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
                HUB_SERIAL + "&isIoTDevice=true&hub_managed=true&optimized=true";
  
  Serial.println("[WS] Connecting to " + WEBSOCKET_HOST + ":" + String(WEBSOCKET_PORT));
  
  webSocket.beginSSL(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, path.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(25000, 5000, 2);
  
  String userAgent = "ESP-Hub-Opt/4.2.0";
  String headers = "User-Agent: " + userAgent;
  webSocket.setExtraHeaders(headers.c_str());
  
  Serial.println("[WS] Setup complete");
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] ✗ DISCONNECTED");
      socketConnected = false;
      devicesRegistered = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WS] ✓ CONNECTED Hub:" + HUB_SERIAL);
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
  
  Serial.println("[REGISTER] Registering " + String(TOTAL_MANAGED_DEVICES) + " devices...");
  
  for (int i = 0; i < TOTAL_MANAGED_DEVICES - 1; i++) { // Skip garden hub in loop
    StaticJsonDocument<512> doc;
    doc["deviceId"] = managedDevices[i];
    doc["serialNumber"] = managedDevices[i];
    doc["deviceType"] = "ESP01_SERVO_DOOR";
    doc["hub_controlled"] = true;
    doc["hub_serial"] = HUB_SERIAL;
    doc["connection_type"] = "esp_now_via_hub";
    doc["door_id"] = i + 1;
    doc["firmware_version"] = "3.2.0";
    doc["esp01_device"] = true;

    String payload;
    serializeJson(doc, payload);
    String fullPayload = "42[\"device_online\"," + payload + "]";
    webSocket.sendTXT(fullPayload);
    
    Serial.println("[REGISTER] Door " + String(i + 1) + ": " + managedDevices[i]);
    delay(100);
  }
  
  // Register garden hub separately
  StaticJsonDocument<512> gardenDoc;
  gardenDoc["deviceId"] = "MEGA27JUN2501GARDEN_HUB_001";
  gardenDoc["serialNumber"] = "MEGA27JUN2501GARDEN_HUB_001";
  gardenDoc["deviceType"] = "MEGA_GARDEN_HUB";
  gardenDoc["hub_controlled"] = true;
  gardenDoc["hub_serial"] = HUB_SERIAL;
  gardenDoc["connection_type"] = "serial_via_hub";
  gardenDoc["firmware_version"] = "3.1.0";
  gardenDoc["garden_system"] = true;

  String gardenPayload;
  serializeJson(gardenDoc, gardenPayload);
  String gardenFullPayload = "42[\"device_online\"," + gardenPayload + "]";
  webSocket.sendTXT(gardenFullPayload);
  
  Serial.println("[REGISTER] Garden Hub: MEGA27JUN2501GARDEN_HUB_001");
  
  devicesRegistered = true;
  Serial.println("[REGISTER] ✓ Complete");
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
    String pongPayload = "42[\"pong\",{\"timestamp\":" + String(millis()) + ",\"hub_serial\":\"" + HUB_SERIAL + "\"}]";
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
  
  // ✅ NEW: Handle different command types
  String action = doc["action"].is<String>() ? doc["action"].as<String>() : "";
  String targetSerial = doc["serialNumber"].is<String>() ? doc["serialNumber"].as<String>() : "";
  String command = doc["command"].is<String>() ? doc["command"].as<String>() : "";
  String target = doc["target"].is<String>() ? doc["target"].as<String>() : "";
  
  Serial.println("[CMD] Action: " + action + " | Target: " + target + " | Command: " + command);
  
  // ✅ GARDEN COMMANDS
  if (action == "garden_command" || action == "garden_pump" || action == "garden_rgb" || 
      action == "garden_automation" || action == "garden_threshold" || target.startsWith("mega_")) {
    
    if (command.length() > 0) {
      Serial.println("[GARDEN] Forward to Mega: " + command);
      Serial.println(command); // Send to Mega via Serial
    } else if (action == "garden_pump") {
      String pumpAction = doc["pump_action"].as<String>();
      Serial.println("GARDEN_CMD:PUMP_" + pumpAction);
    } else if (action == "garden_rgb") {
      String rgbAction = doc["rgb_action"].as<String>();
      Serial.println("GARDEN_CMD:RGB_" + rgbAction);
    }
    return;
  }
  
  // ✅ RELAY COMMANDS
  if (action == "relay_command" || action == "emergency_alarm" || target == "mega_relay" || target == "mega_alarm") {
    if (command.length() > 0) {
      Serial.println("[RELAY] Forward to Mega: " + command);
      Serial.println(command); // Send to Mega via Serial
    }
    return;
  }
  
  // ✅ DOOR COMMANDS (original logic)
  if (targetSerial == "" || action == "") return;
  
  bool isManaged = false;
  for (int i = 0; i < TOTAL_MANAGED_DEVICES - 1; i++) { // Skip garden hub
    if (managedDevices[i] == targetSerial) {
      isManaged = true;
      break;
    }
  }
  
  if (isManaged) {
    Serial.println("[DOOR] ✓ " + targetSerial + " -> " + action);
    Serial.println("CMD:" + targetSerial + ":" + action);
  }
}

void sendDeviceOnline() {
  if (!socketConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["deviceId"] = HUB_SERIAL;
  doc["serialNumber"] = HUB_SERIAL;
  doc["deviceType"] = "HUB_GATEWAY_OPT";
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["hub_managed"] = true;
  doc["hub_id"] = HUB_ID;
  doc["connection_type"] = "hub_optimized";
  doc["managed_devices_count"] = TOTAL_MANAGED_DEVICES;
  doc["garden_support"] = true;

  String payload;
  serializeJson(doc, payload);
  String fullPayload = "42[\"device_online\"," + payload + "]";
  webSocket.sendTXT(fullPayload);
  Serial.println("[ONLINE] Hub registered with garden support");
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
    
    Serial.println("[RECONNECT] Attempt " + String(reconnectAttempts + 1));
    
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

void loop() {
  // Handle config mode
  if (configMode) {
    handleConfigMode();
    return;
  }
  
  // Normal operation
  webSocket.loop();
  checkConnectionStatus();
  
  // Manual ping
  static unsigned long lastManualPing = 0;
  if (socketConnected && millis() - lastManualPing > 30000) {
    webSocket.sendTXT("2");
    lastManualPing = millis();
  }

  // ✅ ENHANCED: Handle MEGA responses (including garden data)
  if (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    response.trim();
    
    if (response.length() == 0) return;
    
    // Filter out status messages to prevent flooding
    if (response.startsWith("HUB_") || 
        response.startsWith("ATMEGA_") ||
        response.indexOf("STATUS") != -1 ||
        response.startsWith("[")) {
      return;
    }
    
    Serial.println("[MEGA-RX] " + response);
    
    // ✅ GARDEN DATA FORWARDING
    if (response.startsWith("GARDEN_DATA:")) {
      String json = response.substring(12);
      String payload = "42[\"garden_sensor_data\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("GARDEN_STATUS:")) {
      String json = response.substring(14);
      String payload = "42[\"garden_automation_status\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("GARDEN_HEALTH:")) {
      String json = response.substring(14);
      String payload = "42[\"garden_system_health\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("PUMP_RESPONSE:")) {
      String json = response.substring(14);
      String payload = "42[\"pump_response\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("GARDEN_RESPONSE:")) {
      String json = response.substring(16);
      String payload = "42[\"garden_command_response\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("GARDEN_AUTOMATION:")) {
      String json = response.substring(18);
      String payload = "42[\"garden_automation_status\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("GARDEN_ONLINE:")) {
      String json = response.substring(14);
      String payload = "42[\"device_online\"," + json + "]";
      webSocket.sendTXT(payload);
      
    // ✅ DOOR/RELAY RESPONSES (original)
    } else if (response.startsWith("RESP:")) {
      String json = response.substring(5);
      String payload = "42[\"command_response\"," + json + "]";
      webSocket.sendTXT(payload);
      
    } else if (response.startsWith("STS:")) {
      String json = response.substring(4);
      String payload = "42[\"deviceStatus\"," + json + "]";
      webSocket.sendTXT(payload);
    }
  }
  
  // Status print
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    Serial.println("[STATUS] Connected:" + String(socketConnected) + 
                   " | Heap:" + String(ESP.getFreeHeap()) + 
                   " | Uptime:" + String(millis() / 1000) + "s" +
                   " | Garden:ENABLED | Config: Hold GPIO0 at boot");
    lastStatus = millis();
  }
  
  yield();
  delay(10);
}