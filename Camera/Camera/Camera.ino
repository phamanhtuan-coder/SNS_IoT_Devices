#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <Base64.h>

// Debug macro
#define DEBUG 0
#if DEBUG
  #define DEBUG_PRINT(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
#endif

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

// JSON buffer sizes (optimized)
constexpr size_t STATUS_JSON_SIZE = JSON_OBJECT_SIZE(10) + 200;
constexpr size_t COMMAND_JSON_SIZE = JSON_OBJECT_SIZE(6) + 150;
constexpr size_t CONFIG_JSON_SIZE = JSON_OBJECT_SIZE(4) + 100;
constexpr size_t CAMERA_ONLINE_JSON_SIZE = JSON_OBJECT_SIZE(8) + 250;

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
int currentQuality = 12;
framesize_t currentFrameSize = FRAMESIZE_QVGA;

// Timing intervals
const unsigned long PING_INTERVAL = 20000;
const unsigned long STATUS_INTERVAL = 30000;
const unsigned long FRAME_INTERVAL = 100;
const unsigned long RECONNECT_BASE_INTERVAL = 10000;
unsigned long reconnectInterval = RECONNECT_BASE_INTERVAL;

// Static buffers
static char websocketPath[128];
static char jsonBuffer[256];

// HTML in PROGMEM (optimized)
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<title>ESP32-CAM Config</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial;padding:10px;background:#f0f0f0}
.container{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:5px}
h1{color:#333;text-align:center;font-size:24px}
.form-group{margin:15px 0}
label{display:block;font-weight:bold;color:#555}
input[type="text"],input[type="password"]{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px}
input[type="submit"]{width:100%;padding:10px;background:#007bff;color:#fff;border:none;border-radius:4px;cursor:pointer}
input[type="submit"]:hover{background:#0056b3}
.status{text-align:center;margin-top:15px;padding:8px;border-radius:4px}
.success{background:#d4edda;color:#155724}
.error{background:#f8d7da;color:#721c24}
</style>
</head><body>
<div class="container">
<h1>ESP32-CAM WiFi Setup</h1>
<form action='/config_wifi' method='POST'>
<div class='form-group'>
<label for='ssid'>WiFi SSID:</label>
<input type='text' id='ssid' name='ssid' required>
</div>
<div class='form-group'>
<label for='password'>Password:</label>
<input type='password' id='password' name='password'>
</div>
<input type='submit' value='Save'>
</form>
<div class="status"><p>Device will restart after saving.</p></div>
</div>
</body></html>
)rawliteral";

// Base64 encoding using library
String base64_encode(uint8_t* data, size_t len) {
  size_t encodedLen = Base64.encodedLength(len);
  char* encoded = new char[encodedLen + 1];
  Base64.encode(encoded, (char*)data, len);
  String result = String(encoded);
  delete[] encoded;
  return result;
}

/**************************************************************
 * EEPROM FUNCTIONS
 **************************************************************/

void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  DEBUG_PRINT(F("[EEPROM] Initialized"));
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
  DEBUG_PRINT(F("[EEPROM] Wrote string"));
}

/**************************************************************
 * WIFI CONFIGURATION
 **************************************************************/

bool connectToWiFi(const String& ssid, const String& password) {
  DEBUG_PRINT(F("[WiFi] Connecting to ") + ssid);
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long startTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
    delay(500);
    DEBUG_PRINT(F("."));
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINT(F("[WiFi] Connected! IP: ") + WiFi.localIP().toString());
    return true;
  }
  
  DEBUG_PRINT(F("[WiFi] Connection failed"));
  return false;
}

void startHotspot() {
  WiFi.softAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  DEBUG_PRINT(F("[Hotspot] Started: ") + String(HOTSPOT_SSID));
  DEBUG_PRINT(F("[Hotspot] IP: ") + WiFi.softAPIP().toString());
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
    
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, packetBuffer);
    
    if (!error && doc.containsKey("ssid") && doc.containsKey("password")) {
      String ssid = doc["ssid"].as<String>();
      String password = doc["password"].as<String>();
      
      DEBUG_PRINT(F("[UDP] Received WiFi credentials"));
      
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
  DEBUG_PRINT(F("[Socket.IO] Joining /camera namespace..."));
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
  
  StaticJsonDocument<250> doc;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["capabilities"]["streaming"] = true;
  doc["capabilities"]["photo_capture"] = true;
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("camera_online", jsonString);
  cameraOnlineSent = true;
}

void sendCameraStatus() {
  if (!namespaceConnected) return;
  
  StaticJsonDocument<200> doc;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["status"] = streamActive ? "streaming" : "idle";
  doc["streamActive"] = streamActive;
  doc["resolution"] = currentFrameSize;
  doc["quality"] = currentQuality;
  doc["clients"] = streamClients;
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("camera_status", jsonString);
}

void sendFrameToServer() {
  if (!namespaceConnected || !streamActive) return;

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    DEBUG_PRINT(F("[Camera] Failed to capture frame"));
    return;
  }

  String frameBase64 = base64_encode(fb->buf, fb->len);
  
  StaticJsonDocument<200> doc;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["type"] = "frame";
  doc["streamKey"] = muxStreamKey;
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
      DEBUG_PRINT(F("[Engine.IO] OPEN"));
      delay(2000);
      joinCameraNamespace();
      break;
    case '2':
      DEBUG_PRINT(F("[Engine.IO] PING"));
      webSocket.sendTXT("3");
      break;
    case '3':
      DEBUG_PRINT(F("[Engine.IO] PONG"));
      break;
    case '4':
      handleSocketIOMessage(message.substring(1));
      break;
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      DEBUG_PRINT(F("[WebSocket] Disconnected"));
      isConnected = false;
      namespaceConnected = false;
      cameraOnlineSent = false;
      streamActive = false;
      reconnectAttempts++;
      reconnectInterval = min(reconnectInterval * 2, 60000UL);
      break;
    case WStype_CONNECTED:
      DEBUG_PRINT(F("[WebSocket] Connected: ") + String((char*)payload));
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
      DEBUG_PRINT(F("[WebSocket] Server ping"));
      break;
    case WStype_PONG:
      DEBUG_PRINT(F("[WebSocket] Server pong"));
      break;
  }
}

void handleSocketIOMessage(const String& socketIOData) {
  if (socketIOData.length() < 1) return;
  
  char socketIOType = socketIOData.charAt(0);
  
  switch(socketIOType) {
    case '0':
      if (socketIOData.indexOf("/camera") != -1) {
        DEBUG_PRINT(F("[Socket.IO] Connected to /camera namespace"));
        namespaceConnected = true;
        delay(3000);
        sendCameraOnline();
      }
      break;
    case '2':
      parseSocketIOEvent(socketIOData.substring(1));
      break;
    case '4':
      DEBUG_PRINT(F("[Socket.IO] ERROR: ") + socketIOData);
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
    
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, jsonData);
    
    if (!err) {
      if (eventName == "command") {
        String action = doc["action"];
        if (action == "setStreamKey") {
          muxStreamKey = doc["params"]["streamKey"].as<String>();
          DEBUG_PRINT(F("[Mux] Received stream key: ") + muxStreamKey);
        } else {
          handleCommand(eventName, doc);
        }
      }
    }
  }
}

void sendCommandResponse(bool success, const String& action, const String& message, const String& additionalData = "") {
  StaticJsonDocument<150> response;
  response["success"] = success;
  response["serialNumber"] = SERIAL_NUMBER;
  response["commandId"] = action;
  response["message"] = message;
  
  if (additionalData.length() > 0) {
    StaticJsonDocument<100> additionalDoc;
    deserializeJson(additionalDoc, additionalData);
    response["data"] = additionalDoc;
  }
  
  String jsonString;
  serializeJson(response, jsonString);
  sendSocketIOEvent("command_response", jsonString);
}

void handleCommand(const String& eventName, JsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    
    if (action == "capture") {
      bool saveToSD = doc["params"]["saveToSD"] | true;
      int quality = doc["params"]["quality"] | 12;
      
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
  DEBUG_PRINT(F("[SD] Initializing SD card..."));
  if (!SD_MMC.begin()) {
    DEBUG_PRINT(F("[SD] Initialization failed!"));
    sdCardAvailable = false;
    return false;
  }
  DEBUG_PRINT(F("[SD] Initialization done."));
  sdCardAvailable = true;
  return true;
}

bool initCamera() {
  DEBUG_PRINT(F("[Camera] Initializing camera..."));
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
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DEBUG_PRINT(F("[Camera] Initialization failed with error 0x") + String(err, HEX));
    return false;
  }
  DEBUG_PRINT(F("[Camera] Initialization done."));
  return true;
}

String savePhotoToSD(bool triggeredByMotion) {
  if (!sdCardAvailable) {
    DEBUG_PRINT(F("[SD] Cannot save photo: SD card not available"));
    return "";
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    DEBUG_PRINT(F "[Camera] Failed to capture photo"));
    return "";
  }

  String filename = triggeredByMotion ? "/motion_" : "/photo_";
  filename += String(millis()) + ".jpg";

  File file = SD_MMC.open(filename, FILE_WRITE);
  if (!file) {
    DEBUG_PRINT(F("[SD] Failed to open file for writing"));
    esp_camera_fb_return(fb);
    return "";
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  DEBUG_PRINT(F("[SD] Photo saved: ") + filename);

  if (namespaceConnected) {
    StaticJsonDocument<150> doc;
    doc["serialNumber"] = SERIAL_NUMBER;
    doc["filename"] = filename;
    doc["size"] = SD_MMC.open(filename).size();
    doc["savedToSD"] = true;
    
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
    "<div style='text-align:center;padding:20px;font-family:Arial'>"
    "<h1>WiFi Saved</h1>"
    "<p>Device will restart and connect to the new network.</p>"
    "</div>");
  
  delay(1000);
  WiFi.softAPdisconnect(true);
  isHotspotMode = false;
  connectToWiFi(ssid, password);
  setupWebSocket();
}

void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["status"] = streamActive ? "streaming" : "idle";
  doc["streamActive"] = streamActive;
  doc["resolution"] = currentFrameSize;
  doc["quality"] = currentQuality;
  doc["clients"] = streamClients;
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["motionDetection"] = motionDetectionEnabled;
  doc["sdCardAvailable"] = sdCardAvailable;
  
  String response;
  serializeJson(doc, response);
  
  sendCorsHeaders();
  httpServer.send(200, "application/json", response);
}

void handleCommandHTTP() {
  String body = httpServer.arg("plain");
  StaticJsonDocument<150> doc;
  
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    httpServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  String action = doc["action"];
  JsonObject params = doc["params"];
  
  StaticJsonDocument<150> response;
  response["success"] = false;
  response["action"] = action;
  
  if (action == "capture") {
    bool saveToSD = params["saveToSD"] | true;
    int quality = params["quality"] | 12;
    
    sensor_t * s = esp_camera_sensor_get();
    s->set_quality(s, quality);
    
    String filename = saveToSD ? savePhotoToSD(false) : "";
    response["success"] = filename !=衣服 || !saveToSD;
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
  StaticJsonDocument<512> doc;
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
  DEBUG_PRINT(F("[HTTP] Server started"));
}

void setupWebSocket() {
  snprintf(websocketPath, sizeof(websocketPath), WEBSOCKET_PATH_TEMPLATE, SERIAL_NUMBER, DEVICE_TYPE);
  webSocket.beginSSL(WEBSOCKET_HOST, WEBSOCKET_PORT, websocketPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(reconnectInterval);
  DEBUG_PRINT(F("[WebSocket] Connecting to: wss://") + String(WEBSOCKET_HOST) + ":" + String(WEBSOCKET_PORT));
}

void checkMotion() {
  if (!motionDetectionEnabled) return;

  static bool lastMotionState = false;
  bool motionState = digitalRead(MOTION_SENSOR_PIN);

  if (motionState && !lastMotionState) {
    DEBUG_PRINT(F("[Motion] Detected!"));
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
  Serial.setDebugOutput(false);
  DEBUG_PRINT(F("\n=== ESP32-CAM v1.3.1 - Optimized ==="));
  DEBUG_PRINT(F("Device Serial: ") + String(SERIAL_NUMBER));
  DEBUG_PRINT(F("Device Type: ") + String(DEVICE_TYPE));
  
  pinMode(MOTION_SENSOR_PIN, INPUT);
  initEEPROM();
  
  String savedSSID = readEEPROMString(WIFI_SSID_ADDR, 100);
  String savedPassword = readEEPROMString(WIFI_PASS_ADDR, 100);
  
  if (savedSSID.length() > 0 && connectToWiFi(savedSSID, savedPassword)) {
    setupWebSocket();
  } else {
    startHotspot();
    udp.begin(UDP_PORT);
    DEBUG_PRINT(F("[UDP] Listening on port ") + String(UDP_PORT));
  }
  
  initSDCard();
  
  if (!initCamera()) {
    DEBUG_PRINT(F("[ERROR] Camera initialization failed. Restarting..."));
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
    if (WiFi.status() != WL_CONNECTED) {
      DEBUG_PRINT(F("[WiFi] Connection lost, reconnecting..."));
      String savedSSID = readEEPROMString(WIFI_SSID_ADDR, 100);
      String savedPassword = readEEPROMString(WIFI_PASS_ADDR, 100);
      
      if (!connectToWiFi(savedSSID, savedPassword)) {
        startHotspot();
        udp.begin(UDP_PORT);
      }
      return;
    }
    
    unsigned long currentTime = millis();
    
    if (isConnected && namespaceConnected && cameraOnlineSent) {
      if (currentTime - lastStatusUpdate > STATUS_INTERVAL) {
        sendCameraStatus();
        lastStatusUpdate = currentTime;
        DEBUG_PRINT(F("[Status] Sent - Free heap: ") + String(ESP.getFreeHeap()) + " bytes");
      }
      
      if (streamActive && currentTime - lastFrameSent > FRAME_INTERVAL) {
        sendFrameToServer();
        lastFrameSent = currentTime;
      }
    } else if (currentTime - lastStatusUpdate > 30000) {
      reconnectAttempts++;
      DEBUG_PRINT(F("[WebSocket] Status - Connected: ") + String(isConnected ? "YES" : "NO"));
      lastStatusUpdate = currentTime;
    }
    
    checkMotion();
  }
  
  delay(5);
}