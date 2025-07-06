#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"
#include "FS.h"
#include <EEPROM.h>
#include <WiFiUdp.h>

// Camera model
#define CAMERA_MODEL_AI_THINKER

// Device identification
#define SERIAL_NUMBER "SERL12ESP32CAM001"
#define DEVICE_TYPE "CAMERA"
#define FIRMWARE_VERSION "1.3.1"

// Hotspot configuration
const char* HOTSPOT_SSID = "ESP32-CAM-Config";
const char* HOTSPOT_PASSWORD = "camconfig123";

// Server configuration
const char* WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
const uint16_t WEBSOCKET_PORT = 443;
const char* WEBSOCKET_PATH_TEMPLATE = "/camera?EIO=4&transport=websocket&serialNumber=%s&deviceType=%s&isIoTDevice=true";

// UDP configuration
#define UDP_PORT 8888
WiFiUDP udp;

// EEPROM configuration
#define EEPROM_SIZE 512
#define WIFI_SSID_ADDR 0
#define WIFI_PASS_ADDR 100

// Motion sensor
#define MOTION_SENSOR_PIN 13

// JSON buffer sizes
constexpr size_t STATUS_JSON_SIZE = JSON_OBJECT_SIZE(12) + 300;
constexpr size_t COMMAND_JSON_SIZE = JSON_OBJECT_SIZE(8) + 200;
constexpr size_t CONFIG_JSON_SIZE = JSON_OBJECT_SIZE(6) + 150;
constexpr size_t CAMERA_ONLINE_JSON_SIZE = JSON_OBJECT_SIZE(10) + 400;

// Global objects
WebServer httpServer(80);
WebSocketsClient webSocket;
camera_config_t camera_config;

// State variables
bool sdCardAvailable = false;
bool streamActive = false;
bool motionDetectionEnabled = false;
int streamClients = 0;
unsigned long lastStatusUpdate = 0;
unsigned long lastPingTime = 0;
unsigned long lastFrameSent = 0;
bool isConnected = false;
bool namespaceConnected = false;
int reconnectAttempts = 0;
bool cameraOnlineSent = false;
bool isHotspotMode = false;
String muxStreamKey = "";

// Camera settings
int currentQuality = 10;
framesize_t currentFrameSize = FRAMESIZE_VGA;

// Timing intervals
const unsigned long PING_INTERVAL = 20000;
const unsigned long STATUS_INTERVAL = 30000;
const unsigned long FRAME_INTERVAL = 100; // 10 FPS
const unsigned long RECONNECT_BASE_INTERVAL = 10000;
unsigned long reconnectInterval = RECONNECT_BASE_INTERVAL;

// Static buffers for optimization
static char websocketPath[256];
static char jsonBuffer[512];

// HTML in PROGMEM to save RAM
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<title>ESP32-CAM Config</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial,sans-serif;padding:20px;background:#f0f0f0}
.container{max-width:400px;margin:0 auto;background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}
h1{color:#333;text-align:center;margin-bottom:30px}
.form-group{margin:20px 0}
label{display:block;margin-bottom:5px;font-weight:bold;color:#555}
input[type="text"],input[type="password"]{width:100%;padding:12px;border:1px solid #ddd;border-radius:4px;font-size:16px}
input[type="submit"]{width:100%;padding:12px;background:#007bff;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer}
input[type="submit"]:hover{background:#0056b3}
.status{text-align:center;margin-top:20px;padding:10px;border-radius:4px}
.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}
</style>
</head><body>
<div class="container">
<h1>ESP32-CAM WiFi Setup</h1>
<form action='/config_wifi' method='POST'>
<div class='form-group'>
<label for='ssid'>WiFi Network Name (SSID):</label>
<input type='text' id='ssid' name='ssid' required placeholder='Enter WiFi name'>
</div>
<div class='form-group'>
<label for='password'>WiFi Password:</label>
<input type='password' id='password' name='password' placeholder='Enter WiFi password'>
</div>
<input type='submit' value='Save and Connect'>
</form>
<div class="status">
<p>Device will restart and connect to the specified network after saving.</p>
</div>
</div>
</body></html>
)rawliteral";

// Base64 encoding functions
const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
  String ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for(i = 0; (i <4) ; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i) {
    for(j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; (j < i + 1); j++)
      ret += base64_chars[char_array_4[j]];

    while((i++ < 3))
      ret += '=';
  }

  return ret;
}

/**************************************************************
 * EEPROM FUNCTIONS
 **************************************************************/

void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  Serial.println(F("[EEPROM] Initialized"));
}

String readEEPROMString(int address, int maxLength) {
  String result = "";
  result.reserve(maxLength);
  for (int i = 0; i < maxLength; i++) {
    char c = EEPROM.read(address + i);
    if (c == 0) break;
    result += c;
  }
  return result;
}

void writeEEPROMString(int address, const String& data, int maxLength) {
  int len = min((int)data.length(), maxLength - 1);
  for (int i = 0; i < len; i++) {
    EEPROM.write(address + i, data[i]);
  }
  EEPROM.write(address + len, 0);
  EEPROM.commit();
}

/**************************************************************
 * WIFI CONFIGURATION
 **************************************************************/

bool connectToWiFi(const String& ssid, const String& password) {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.println(ssid);
  
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long startTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
    delay(500);
    Serial.print(F("."));
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WiFi] Connected! IP: "));
    Serial.println(WiFi.localIP());
    return true;
  }
  
  Serial.println(F("\n[WiFi] Connection failed"));
  return false;
}

void startHotspot() {
  WiFi.softAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  Serial.print(F("[Hotspot] Started: "));
  Serial.println(HOTSPOT_SSID);
  Serial.print(F("[Hotspot] IP: "));
  Serial.println(WiFi.softAPIP());
  isHotspotMode = true;
}

/**************************************************************
 * UDP FUNCTIONS
 **************************************************************/

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[255];
    int len = udp.read(packetBuffer, 255);
    if (len > 0) packetBuffer[len] = 0;
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, packetBuffer);
    
    if (!error && doc.containsKey("ssid") && doc.containsKey("password")) {
      String ssid = doc["ssid"].as<String>();
      String password = doc["password"].as<String>();
      
      Serial.println(F("[UDP] Received WiFi credentials"));
      
      writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
      writeEEPROMString(WIFI_PASS_ADDR, password, 100);
      
      const char* response = "{\"status\":\"success\",\"message\":\"Credentials received\"}";
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.print(response);
      udp.endPacket();
      
      WiFi.softAPdisconnect(true);
      isHotspotMode = false;
      if (connectToWiFi(ssid, password)) {
        setupWebSocket();
      } else {
        startHotspot();
      }
    }
  }
}

/**************************************************************
 * WEBSOCKET FUNCTIONS
 **************************************************************/

void joinCameraNamespace() {
  Serial.println(F("[Socket.IO] Joining /camera namespace..."));
  webSocket.sendTXT("40/camera,");
}

void sendSocketIOEvent(const char* eventName, const String& jsonData) {
  if (!namespaceConnected) return;
  
  String eventPayload = "42/camera,[\"";
  eventPayload += eventName;
  eventPayload += "\",";
  eventPayload += jsonData;
  eventPayload += "]";
  webSocket.sendTXT(eventPayload);
}

void sendCameraOnline() {
  if (!namespaceConnected || cameraOnlineSent) return;
  
  DynamicJsonDocument doc(CAMERA_ONLINE_JSON_SIZE);
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["capabilities"]["streaming"] = true;
  doc["capabilities"]["photo_capture"] = true;
  doc["capabilities"]["motion_detection"] = true;
  doc["capabilities"]["sd_storage"] = sdCardAvailable;
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("camera_online", jsonString);
  cameraOnlineSent = true;
}

void sendCameraStatus() {
  if (!namespaceConnected) return;
  
  DynamicJsonDocument doc(STATUS_JSON_SIZE);
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["status"] = streamActive ? "streaming" : "idle";
  doc["streamActive"] = streamActive;
  doc["resolution"] = currentFrameSize;
  doc["quality"] = currentQuality;
  doc["clients"] = streamClients;
  doc["motionDetectionEnabled"] = motionDetectionEnabled;
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("camera_status", jsonString);
}

void sendFrameToServer() {
  if (!namespaceConnected || !streamActive) return;

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println(F("[Camera] Failed to capture frame"));
    return;
  }

  String frameBase64 = base64_encode(fb->buf, fb->len);
  
  DynamicJsonDocument doc(256);
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["type"] = "frame";
  doc["timestamp"] = millis();
  doc["data"] = frameBase64;

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("stream_frame", jsonString);

  esp_camera_fb_return(fb);
}

void handleWebSocketMessage(const String& message) {
  if (message.length() < 1) return;
  
  char engineIOType = message.charAt(0);
  
  switch(engineIOType) {
    case '0':
      Serial.println(F("[Engine.IO] OPEN"));
      delay(2000);
      joinCameraNamespace();
      break;
    case '2':
      Serial.println(F("[Engine.IO] PING"));
      webSocket.sendTXT("3");
      break;
    case '3':
      Serial.println(F("[Engine.IO] PONG"));
      break;
    case '4':
      handleSocketIOMessage(message.substring(1));
      break;
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WebSocket] Disconnected. Attempts: %d\n", reconnectAttempts);
      isConnected = false;
      namespaceConnected = false;
      cameraOnlineSent = false;
      streamActive = false;
      reconnectAttempts++;
      reconnectInterval = min(reconnectInterval * 2, 60000UL);
      break;
    case WStype_CONNECTED:
      Serial.printf("[WebSocket] Connected: %s\n", payload);
      isConnected = true;
      namespaceConnected = false;
      cameraOnlineSent = false;
      reconnectAttempts = 0;
      reconnectInterval = RECONNECT_BASE_INTERVAL;
      break;
    case WStype_TEXT:
      handleWebSocketMessage(String((char*)payload));
      break;
    case WStype_PING:
      Serial.println(F("[WebSocket] Server ping"));
      break;
    case WStype_PONG:
      Serial.println(F("[WebSocket] Server pong"));
      break;
    default:
      break;
  }
}

void handleSocketIOMessage(const String& socketIOData) {
  if (socketIOData.length() < 1) return;
  
  char socketIOType = socketIOData.charAt(0);
  
  switch(socketIOType) {
    case '0':
      if (socketIOData.indexOf("/camera") != -1) {
        Serial.println(F("[Socket.IO] Connected to /camera namespace"));
        namespaceConnected = true;
        delay(3000);
        sendCameraOnline();
      }
      break;
    case '2':
      parseSocketIOEvent(socketIOData.substring(1));
      break;
    case '4':
      Serial.println(F("[Socket.IO] ERROR: "));
      Serial.println(socketIOData);
      if (!namespaceConnected) {
        delay(3000);
        joinCameraNamespace();
      }
      break;
  }
}

void parseSocketIOEvent(const String& eventData) {
  String processedData = eventData;
  if (processedData.startsWith("/camera,")) {
    processedData = processedData.substring(8);
  }
  
  int firstBracket = processedData.indexOf('[');
  if (firstBracket == -1) return;
  
  int firstQuote = processedData.indexOf('"', firstBracket);
  if (firstQuote == -1) return;
  
  int secondQuote = processedData.indexOf('"', firstQuote + 1);
  if (secondQuote == -1) return;
  
  String eventName = processedData.substring(firstQuote + 1, secondQuote);
  
  int jsonStart = processedData.indexOf('{');
  if (jsonStart != -1) {
    String jsonData = processedData.substring(jsonStart, processedData.lastIndexOf('}') + 1);
    
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, jsonData);
    
    if (!err) {
      if (eventName == "command") {
        String action = doc["action"];
        if (action == "setStreamKey") {
          muxStreamKey = doc["params"]["streamKey"].as<String>();
          Serial.print(F("[Mux] Received stream key: "));
          Serial.println(muxStreamKey);
        } else {
          handleCommand(eventName, doc);
        }
      }
    }
  }
}

void sendCommandResponse(bool success, const String& action, const String& message, const String& additionalData = "") {
  DynamicJsonDocument response(COMMAND_JSON_SIZE);
  response["success"] = success;
  response["serialNumber"] = SERIAL_NUMBER;
  response["commandId"] = action;
  response["timestamp"] = millis();
  response["message"] = message;
  
  if (additionalData.length() > 0) {
    DynamicJsonDocument additionalDoc(256);
    deserializeJson(additionalDoc, additionalData);
    response["data"] = additionalDoc;
  }
  
  String jsonString;
  serializeJson(response, jsonString);
  sendSocketIOEvent("command_response", jsonString);
}

void handleCommand(const String& eventName, DynamicJsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    
    if (action == "capture") {
      bool saveToSD = doc["params"]["saveToSD"] | true;
      int quality = doc["params"]["quality"] | 10;
      
      sensor_t * s = esp_camera_sensor_get();
      s->set_quality(s, quality);
      
      String filename = saveToSD ? savePhotoToSD(false) : "";
      bool success = filename != "" || !saveToSD;
      
      String additionalData = "{\"filename\":\"" + filename + "\",\"size\":" + 
                             (filename != "" ? String(SD_MMC.open(filename).size()) : "0") + "}";
      
      sendCommandResponse(success, action, 
                         success ? "Photo captured" : "Capture failed", 
                         additionalData);
    }
    else if (action == "setResolution") {
      framesize_t frameSize = (framesize_t)doc["params"]["size"].as<int>();
      sensor_t * s = esp_camera_sensor_get();
      if (s->set_framesize(s, frameSize) == 0) {
        currentFrameSize = frameSize;
        sendCommandResponse(true, action, "Resolution updated");
      } else {
        sendCommandResponse(false, action, "Failed to set resolution");
      }
    }
    else if (action == "setQuality") {
      int quality = doc["params"]["quality"];
      sensor_t * s = esp_camera_sensor_get();
      if (s->set_quality(s, quality) == 0) {
        currentQuality = quality;
        sendCommandResponse(true, action, "Quality updated");
      } else {
        sendCommandResponse(false, action, "Failed to set quality");
      }
    }
    else if (action == "toggleMotion") {
      motionDetectionEnabled = doc["params"]["enabled"] | false;
      sendCommandResponse(true, action, 
                         "Motion detection " + String(motionDetectionEnabled ? "enabled" : "disabled"));
    }
    else if (action == "startStream") {
      streamActive = true;
      sendCommandResponse(true, action, "Stream started");
    }
    else if (action == "stopStream") {
      streamActive = false;
      sendCommandResponse(true, action, "Stream stopped");
    }
    else {
      sendCommandResponse(false, action, "Unknown command: " + action);
    }
  }
}

/**************************************************************
 * CAMERA AND SD CARD FUNCTIONS
 **************************************************************/

bool initSDCard() {
  Serial.println(F("[SD] Initializing SD card..."));
  if (!SD_MMC.begin()) {
    Serial.println(F("[SD] Initialization failed!"));
    sdCardAvailable = false;
    return false;
  }
  Serial.println(F("[SD] Initialization done."));
  sdCardAvailable = true;
  return true;
}

bool initCamera() {
  Serial.println(F("[Camera] Initializing camera..."));
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[Camera] Initialization failed with error 0x%x\n", err);
    return false;
  }
  Serial.println(F("[Camera] Initialization done."));
  return true;
}

String savePhotoToSD(bool triggeredByMotion) {
  if (!sdCardAvailable) {
    Serial.println(F("[SD] Cannot save photo: SD card not available"));
    return "";
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println(F("[Camera] Failed to capture photo"));
    return "";
  }

  String filename = triggeredByMotion ? "/motion_" : "/photo_";
  filename += String(millis()) + ".jpg";

  File file = SD_MMC.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("[SD] Failed to open file for writing"));
    esp_camera_fb_return(fb);
    return "";
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  Serial.print(F("[SD] Photo saved: "));
  Serial.println(filename);

  if (namespaceConnected) {
    DynamicJsonDocument doc(256);
    doc["serialNumber"] = SERIAL_NUMBER;
    doc["filename"] = filename;
    doc["size"] = SD_MMC.open(filename).size();
    doc["savedToSD"] = true;
    doc["timestamp"] = millis();
    
    String jsonString;
    serializeJson(doc, jsonString);
    sendSocketIOEvent("photo_captured", jsonString);
  }

  return filename;
}

/**************************************************************
 * HTTP SERVER HANDLERS
 **************************************************************/

void sendCorsHeaders() {
  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleRoot() {
  httpServer.send_P(200, "text/html", CONFIG_HTML);
}

void handleWifiConfig() {
  String ssid = httpServer.arg("ssid");
  String password = httpServer.arg("password");
  
  writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
  writeEEPROMString(WIFI_PASS_ADDR, password, 100);
  
  httpServer.send(200, "text/html", 
    "<div style='text-align:center;padding:50px;font-family:Arial'>"
    "<h1>WiFi Configuration Saved</h1>"
    "<p>Device will restart and connect to the new network.</p>"
    "<p>Please connect to the new network and access the device via its new IP address.</p>"
    "</div>");
  
  delay(1000);
  WiFi.softAPdisconnect(true);
  isHotspotMode = false;
  connectToWiFi(ssid, password);
  setupWebSocket();
}

void handleStatus() {
  DynamicJsonDocument doc(STATUS_JSON_SIZE);
  doc["status"] = streamActive ? "streaming" : "idle";
  doc["streamActive"] = streamActive;
  doc["resolution"] = currentFrameSize;
  doc["quality"] = currentQuality;
  doc["clients"] = streamClients;
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["motionDetection"] = motionDetectionEnabled;
  doc["sdCardAvailable"] = sdCardAvailable;
  doc["timestamp"] = millis();
  
  String response;
  serializeJson(doc, response);
  
  sendCorsHeaders();
  httpServer.send(200, "application/json", response);
}

void handleCommandHTTP() {
  String body = httpServer.arg("plain");
  DynamicJsonDocument doc(COMMAND_JSON_SIZE);
  
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    httpServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  String action = doc["action"];
  JsonObject params = doc["params"];
  
  DynamicJsonDocument response(COMMAND_JSON_SIZE);
  response["success"] = false;
  response["action"] = action;
  response["timestamp"] = millis();
  
  if (action == "capture") {
    bool saveToSD = params["saveToSD"] | true;
    int quality = params["quality"] | 10;
    
    sensor_t * s = esp_camera_sensor_get();
    s->set_quality(s, quality);
    
    String filename = saveToSD ? savePhotoToSD(false) : "";
    response["success"] = filename != "" || !saveToSD;
    response["filename"] = filename;
    response["size"] = filename != "" ? SD_MMC.open(filename).size() : 0;
    response["message"] = filename != "" ? "Photo captured" : "Capture failed";
  }
  else if (action == "setResolution") {
    framesize_t frameSize = (framesize_t)params["size"].as<int>();
    sensor_t * s = esp_camera_sensor_get();
    if (s->set_framesize(s, frameSize) == 0) {
      currentFrameSize = frameSize;
      response["success"] = true;
      response["message"] = "Resolution updated";
    }
  }
  else if (action == "setQuality") {
    int quality = params["quality"];
    sensor_t * s = esp_camera_sensor_get();
    if (s->set_quality(s, quality) == 0) {
      currentQuality = quality;
      response["success"] = true;
      response["message"] = "Quality updated";
    }
  }
  else if (action == "toggleMotion") {
    motionDetectionEnabled = params["enabled"] | false;
    response["success"] = true;
    response["message"] = "Motion detection " + String(motionDetectionEnabled ? "enabled" : "disabled");
  }
  else if (action == "startStream") {
    streamActive = true;
    response["success"] = true;
    response["message"] = "Stream started";
  }
  else if (action == "stopStream") {
    streamActive = false;
    response["success"] = true;
    response["message"] = "Stream stopped";
  }
  else {
    response["message"] = "Unknown action: " + action;
  }
  
  String responseStr;
  serializeJson(response, responseStr);
  sendCorsHeaders();
  httpServer.send(200, "application/json", responseStr);
}

void handlePhotos() {
  DynamicJsonDocument doc(1024);
  JsonArray photosArray = doc.createNestedArray("photos");
  
  if (sdCardAvailable) {
    File root = SD_MMC.open("/");
    File file = root.openNextFile();
    int count = 0;
    int limit = httpServer.arg("limit").toInt();
    if (limit == 0) limit = 20;
    
    while (file && count < limit) {
      if (!file.isDirectory() && String(file.name()).endsWith(".jpg")) {
        JsonObject photo = photosArray.createNestedObject();
        photo["filename"] = String(file.name());
        photo["size"] = file.size();
        photo["timestamp"] = file.getLastWrite() * 1000;
        count++;
      }
      file = root.openNextFile();
    }
  }
  
  doc["total"] = photosArray.size();
  
  String response;
  serializeJson(doc, response);
  sendCorsHeaders();
  httpServer.send(200, "application/json", response);
}

void handleCors() {
  sendCorsHeaders();
  httpServer.send(200);
}

void handleNotFound() {
  httpServer.send(404, "text/plain", "Not found");
}

/**************************************************************
 * HTTP SERVER SETUP
 **************************************************************/

void setupBasicRoutes() {
  httpServer.on("/", HTTP_GET, handleRoot);
  httpServer.on("/config_wifi", HTTP_POST, handleWifiConfig);
  httpServer.on("/status", HTTP_GET, handleStatus);
}

void setupApiRoutes() {
  httpServer.on("/command", HTTP_POST, handleCommandHTTP);
  httpServer.on("/photos", HTTP_GET, handlePhotos);
}

void setupCorsRoutes() {
  httpServer.on("/command", HTTP_OPTIONS, handleCors);
  httpServer.on("/config", HTTP_OPTIONS, handleCors);
}

void setupWebServer() {
  setupBasicRoutes();
  setupApiRoutes();
  setupCorsRoutes();
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  Serial.println(F("[HTTP] Server started"));
}

void setupWebSocket() {
  snprintf(websocketPath, sizeof(websocketPath), WEBSOCKET_PATH_TEMPLATE, SERIAL_NUMBER, DEVICE_TYPE);
  webSocket.beginSSL(WEBSOCKET_HOST, WEBSOCKET_PORT, websocketPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(reconnectInterval);
  Serial.print(F("[WebSocket] Connecting to: wss://"));
  Serial.print(WEBSOCKET_HOST);
  Serial.print(":");
  Serial.println(WEBSOCKET_PORT);
}

void checkMotion() {
  if (!motionDetectionEnabled) return;

  static bool lastMotionState = false;
  bool motionState = digitalRead(MOTION_SENSOR_PIN);

  if (motionState && !lastMotionState) {
    Serial.println(F("[Motion] Detected!"));
    if (sdCardAvailable) {
      savePhotoToSD(true);
    }
  }

  lastMotionState = motionState;
}

/**************************************************************
 * SETUP AND LOOP
 **************************************************************/

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false); // Reduce debug output for faster operation
  Serial.println(F("\n=== ESP32-CAM v1.3.1 - Optimized ==="));
  Serial.print(F("Device Serial: "));
  Serial.println(SERIAL_NUMBER);
  Serial.print(F("Device Type: "));
  Serial.println(DEVICE_TYPE);
  
  pinMode(MOTION_SENSOR_PIN, INPUT);
  initEEPROM();
  
  String savedSSID = readEEPROMString(WIFI_SSID_ADDR, 100);
  String savedPassword = readEEPROMString(WIFI_PASS_ADDR, 100);
  
  if (savedSSID.length() > 0 && connectToWiFi(savedSSID, savedPassword)) {
    setupWebSocket();
  } else {
    startHotspot();
    udp.begin(UDP_PORT);
    Serial.print(F("[UDP] Listening on port "));
    Serial.println(UDP_PORT);
  }
  
  initSDCard();
  
  if (!initCamera()) {
    Serial.println(F("[ERROR] Camera initialization failed. Restarting..."));
    ESP.restart();
  }
  
  setupWebServer();
  lastStatusUpdate = millis();
  lastFrameSent = millis();
}

void loop() {
  webSocket.loop();
  httpServer.handleClient();
  
  if (isHotspotMode) {
    handleUDP();
  } else {
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[WiFi] Connection lost, reconnecting..."));
      String savedSSID = readEEPROMString(WIFI_SSID_ADDR, 100);
      String savedPassword = readEEPROMString(WIFI_PASS_ADDR, 100);
      
      if (!connectToWiFi(savedSSID, savedPassword)) {
        startHotspot();
        udp.begin(UDP_PORT);
      }
      return;
    }
    
    unsigned long currentTime = millis();
    
    // Handle connected state operations
    if (isConnected && namespaceConnected && cameraOnlineSent) {
      // Send status updates
      if (currentTime - lastStatusUpdate > STATUS_INTERVAL) {
        sendCameraStatus();
        lastStatusUpdate = currentTime;
        Serial.printf("[Status] Sent - Free heap: %d bytes, RSSI: %d dBm\n", 
                     ESP.getFreeHeap(), WiFi.RSSI());
      }
      
      // Send video frames with rate limiting
      if (streamActive && currentTime - lastFrameSent > FRAME_INTERVAL) {
        sendFrameToServer();
        lastFrameSent = currentTime;
      }
    } else if (currentTime - lastStatusUpdate > 30000) {
      // Debug connection state
      reconnectAttempts++;
      Serial.printf("[WebSocket] Status - Connected: %s, Namespace: %s, Online Sent: %s\n",
                    isConnected ? "YES" : "NO", 
                    namespaceConnected ? "YES" : "NO",
                    cameraOnlineSent ? "YES" : "NO");
      lastStatusUpdate = currentTime;
    }
    
    // Check motion detection
    checkMotion();
  }
  
  // Small delay to prevent WDT reset
  delay(5);
}