#include "esp_camera.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "fr_flash.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <Base64.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Debug configuration
#define DEBUG 1
#if DEBUG
  #define DEBUG_PRINT(x) Serial.println(x)
  #define DEBUG_PRINTF(x, ...) Serial.printf(x, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTF(x, ...)
#endif

// Camera model - AI Thinker
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// Device configuration
#define SERIAL_NUMBER "SERL12ESP32CAM001"
#define DEVICE_TYPE "CAMERA"
#define FIRMWARE_VERSION "1.4.0"

// Network configuration
const char* HOTSPOT_SSID = "ESP32-CAM-Config";
const char* HOTSPOT_PASSWORD = "camconfig123";
const char* WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
const uint16_t WEBSOCKET_PORT = 443;
const char* WEBSOCKET_PATH_TEMPLATE = "/camera?EIO=4&transport=websocket&serialNumber=%s&deviceType=%s&isIoTDevice=true";

// Hardware pins
#define MOTION_SENSOR_PIN 13
#define LED_PIN 4
#define FLASH_PIN 4

// Network & Storage
#define UDP_PORT 8888
#define EEPROM_SIZE 512
#define WIFI_SSID_ADDR 0
#define WIFI_PASS_ADDR 100
#define CONFIG_ADDR 200

// Timing constants
const unsigned long PING_INTERVAL = 30000;
const unsigned long STATUS_INTERVAL = 60000;
const unsigned long FRAME_INTERVAL = 100;
const unsigned long RECONNECT_BASE_INTERVAL = 10000;
const unsigned long WATCHDOG_TIMEOUT = 120000;

// Face detection configuration
static mtmn_config_t mtmn_config = {0};
static face_id_name_list st_face_list;
static dl_matrix3du_t *aligned_face = NULL;

// Global objects
WebServer httpServer(80);
WebSocketsClient webSocket;
WiFiUDP udp;
camera_config_t camera_config;

// State variables
struct CameraState {
  bool sdCardAvailable = false;
  bool streamActive = false;
  bool motionDetectionEnabled = false;
  bool faceDetectionEnabled = false;
  bool rtmpStreamActive = false;
  bool isConnected = false;
  bool namespaceConnected = false;
  bool cameraOnlineSent = false;
  bool isHotspotMode = false;
  int streamClients = 0;
  int reconnectAttempts = 0;
  unsigned long reconnectInterval = RECONNECT_BASE_INTERVAL;
  unsigned long lastStatusUpdate = 0;
  unsigned long lastPingTime = 0;
  unsigned long lastWatchdog = 0;
} state;

// Camera settings
struct CameraSettings {
  int quality = 12;
  framesize_t frameSize = FRAMESIZE_QVGA;
  int brightness = 0;
  int contrast = 0;
  int saturation = 0;
  bool autoWB = true;
  bool autoExposure = true;
} settings;

// Network credentials
String wifiSSID = "";
String wifiPassword = "";
String muxStreamKey = "";
String rtmpUrl = "";

// Buffers
static char websocketPath[256];
static char jsonBuffer[512];

// HTML Configuration Page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<title>ESP32-CAM Setup</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}
.container{max-width:500px;margin:0 auto;background:#fff;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}
h1{color:#333;text-align:center;margin-bottom:30px}
.form-group{margin-bottom:20px}
label{display:block;margin-bottom:5px;font-weight:bold;color:#555}
input[type="text"],input[type="password"],select{width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;font-size:16px;box-sizing:border-box}
input[type="submit"]{width:100%;padding:12px;background:#007bff;color:#fff;border:none;border-radius:5px;font-size:16px;cursor:pointer;transition:background 0.3s}
input[type="submit"]:hover{background:#0056b3}
.info{background:#e9ecef;padding:15px;border-radius:5px;margin-bottom:20px}
.status{text-align:center;margin-top:20px;padding:10px;border-radius:5px;font-weight:bold}
.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}
.features{margin-top:20px}
.feature{display:flex;align-items:center;margin-bottom:10px}
.feature input[type="checkbox"]{margin-right:10px}
</style>
</head><body>
<div class="container">
<h1>🎥 ESP32-CAM Setup</h1>
<div class="info">
<strong>Device:</strong> %s<br>
<strong>Version:</strong> %s<br>
<strong>Features:</strong> Face Detection, Motion Detection, RTMP Streaming
</div>
<form action='/save_config' method='POST'>
<div class='form-group'>
<label for='ssid'>WiFi Network:</label>
<input type='text' id='ssid' name='ssid' required placeholder="Enter WiFi SSID">
</div>
<div class='form-group'>
<label for='password'>WiFi Password:</label>
<input type='password' id='password' name='password' placeholder="Enter WiFi Password">
</div>
<div class='form-group'>
<label for='quality'>Image Quality (1-63, lower=better):</label>
<select id='quality' name='quality'>
<option value='10'>High (10)</option>
<option value='12' selected>Medium (12)</option>
<option value='15'>Low (15)</option>
</select>
</div>
<div class="features">
<div class="feature">
<input type="checkbox" id="faceDetection" name="faceDetection" checked>
<label for="faceDetection">Enable Face Detection</label>
</div>
<div class="feature">
<input type="checkbox" id="motionDetection" name="motionDetection" checked>
<label for="motionDetection">Enable Motion Detection</label>
</div>
</div>
<input type='submit' value='💾 Save Configuration'>
</form>
<div class="status">
<p>📡 Device will restart after saving configuration</p>
</div>
</div>
</body></html>
)rawliteral";

// =================== EEPROM FUNCTIONS ===================

void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  DEBUG_PRINT("[EEPROM] Initialized");
}

String readEEPROMString(int address, int maxLength) {
  String result = "";
  result.reserve(maxLength);
  for (int i = 0; i < maxLength && i < maxLength; i++) {
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
  DEBUG_PRINTF("[EEPROM] Wrote string at %d: %s\n", address, data.c_str());
}

void saveConfiguration() {
  StaticJsonDocument<200> config;
  config["quality"] = settings.quality;
  config["frameSize"] = settings.frameSize;
  config["faceDetection"] = state.faceDetectionEnabled;
  config["motionDetection"] = state.motionDetectionEnabled;
  
  String configStr;
  serializeJson(config, configStr);
  writeEEPROMString(CONFIG_ADDR, configStr, 200);
}

void loadConfiguration() {
  String configStr = readEEPROMString(CONFIG_ADDR, 200);
  if (configStr.length() > 0) {
    StaticJsonDocument<200> config;
    if (deserializeJson(config, configStr) == DeserializationError::Ok) {
      settings.quality = config["quality"] | 12;
      settings.frameSize = (framesize_t)(config["frameSize"] | FRAMESIZE_QVGA);
      state.faceDetectionEnabled = config["faceDetection"] | true;
      state.motionDetectionEnabled = config["motionDetection"] | true;
      DEBUG_PRINT("[Config] Loaded from EEPROM");
    }
  }
}

// =================== WIFI FUNCTIONS ===================

bool connectToWiFi(const String& ssid, const String& password) {
  DEBUG_PRINTF("[WiFi] Connecting to %s...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
    delay(500);
    DEBUG_PRINT(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTF("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("[WiFi] Signal: %d dBm\n", WiFi.RSSI());
    return true;
  }
  
  DEBUG_PRINT("[WiFi] Connection failed");
  return false;
}

void startHotspot() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  DEBUG_PRINTF("[Hotspot] Started: %s\n", HOTSPOT_SSID);
  DEBUG_PRINTF("[Hotspot] IP: %s\n", WiFi.softAPIP().toString().c_str());
  state.isHotspotMode = true;
}

// =================== UDP FUNCTIONS ===================

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[512];
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = 0;
      DEBUG_PRINTF("[UDP] Received: %s\n", packetBuffer);
      
      StaticJsonDocument<300> doc;
      DeserializationError error = deserializeJson(doc, packetBuffer);
      
      if (!error && doc.containsKey("ssid")) {
        String ssid = doc["ssid"].as<String>();
        String password = doc["password"].as<String>();
        int quality = doc["quality"] | 12;
        bool faceDetection = doc["faceDetection"] | true;
        bool motionDetection = doc["motionDetection"] | true;
        
        DEBUG_PRINT("[UDP] Received WiFi credentials and config");
        
        // Save WiFi credentials
        writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
        writeEEPROMString(WIFI_PASS_ADDR, password, 100);
        
        // Save configuration
        settings.quality = quality;
        state.faceDetectionEnabled = faceDetection;
        state.motionDetectionEnabled = motionDetection;
        saveConfiguration();
        
        // Send response
        StaticJsonDocument<150> response;
        response["status"] = "success";
        response["message"] = "Configuration received";
        response["device"] = SERIAL_NUMBER;
        
        String responseStr;
        serializeJson(response, responseStr);
        
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.print(responseStr);
        udp.endPacket();
        
        // Restart with new configuration
        delay(2000);
        ESP.restart();
      }
    }
  }
}

// =================== FACE DETECTION ===================

void initFaceDetection() {
  DEBUG_PRINT("[Face] Initializing face detection...");
  
  mtmn_config.type = FAST;
  mtmn_config.min_face = 80;
  mtmn_config.pyramid = 0.707;
  mtmn_config.pyramid_times = 4;
  mtmn_config.p_threshold.score = 0.6;
  mtmn_config.p_threshold.nms = 0.7;
  mtmn_config.p_threshold.candidate_number = 20;
  mtmn_config.r_threshold.score = 0.7;
  mtmn_config.r_threshold.nms = 0.7;
  mtmn_config.r_threshold.candidate_number = 10;
  mtmn_config.o_threshold.score = 0.7;
  mtmn_config.o_threshold.nms = 0.7;
  mtmn_config.o_threshold.candidate_number = 1;
  
  face_id_init(&st_face_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);
  DEBUG_PRINT("[Face] Face detection initialized");
}

String processFaceDetection(camera_fb_t *fb) {
  if (!state.faceDetectionEnabled || !fb) return "";
  
  unsigned long startTime = millis();
  
  // Convert JPEG to RGB888
  dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
  if (!image_matrix) {
    DEBUG_PRINT("[Face] Failed to allocate image matrix");
    return "";
  }

  if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) {
    DEBUG_PRINT("[Face] Failed to convert image format");
    dl_matrix3du_free(image_matrix);
    return "";
  }
  
  // Detect faces
  box_array_t *net_boxes = face_detect(image_matrix, &mtmn_config);
  
  String detectionResult = "";
  if (net_boxes && net_boxes->len > 0) {
    StaticJsonDocument<1024> doc;
    JsonArray faces = doc.createNestedArray("faces");
    
    for (int i = 0; i < net_boxes->len; i++) {
      JsonObject face = faces.createNestedObject();
      face["x"] = net_boxes->box[i].box_p[0];
      face["y"] = net_boxes->box[i].box_p[1]; 
      face["width"] = net_boxes->box[i].box_p[2] - net_boxes->box[i].box_p[0];
      face["height"] = net_boxes->box[i].box_p[3] - net_boxes->box[i].box_p[1];
      face["confidence"] = round(net_boxes->box[i].score * 100) / 100.0;
    }
    
    doc["count"] = net_boxes->len;
    doc["processing_time"] = millis() - startTime;
    doc["timestamp"] = millis();
    
    serializeJson(doc, detectionResult);
    free(net_boxes);
    
    DEBUG_PRINTF("[Face] Detected %d face(s) in %lu ms\n", net_boxes->len, millis() - startTime);
  }
  
  dl_matrix3du_free(image_matrix);
  return detectionResult;
}

// =================== CAMERA FUNCTIONS ===================

bool initCamera() {
  DEBUG_PRINT("[Camera] Initializing camera with AI features...");
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = settings.frameSize;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = settings.quality;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DEBUG_PRINTF("[Camera] Init failed with error 0x%x\n", err);
    return false;
  }
  
  // Apply initial settings
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, settings.brightness);
    s->set_contrast(s, settings.contrast);
    s->set_saturation(s, settings.saturation);
    s->set_whitebal(s, settings.autoWB);
    s->set_exposure_ctrl(s, settings.autoExposure);
  }
  
  initFaceDetection();
  
  DEBUG_PRINT("[Camera] Camera initialized successfully");
  return true;
}

bool initSDCard() {
  DEBUG_PRINT("[SD] Initializing SD card...");
  if (!SD_MMC.begin()) {
    DEBUG_PRINT("[SD] Initialization failed!");
    state.sdCardAvailable = false;
    return false;
  }
  
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    DEBUG_PRINT("[SD] No SD card attached");
    state.sdCardAvailable = false;
    return false;
  }
  
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  DEBUG_PRINTF("[SD] Card size: %lluMB\n", cardSize);
  state.sdCardAvailable = true;
  return true;
}

String savePhotoToSD(camera_fb_t *fb, bool triggeredByMotion = false) {
  if (!state.sdCardAvailable || !fb) {
    DEBUG_PRINT("[SD] Cannot save photo");
    return "";
  }

  String filename = triggeredByMotion ? "/motion_" : "/photo_";
  filename += String(millis()) + ".jpg";

  File file = SD_MMC.open(filename, FILE_WRITE);
  if (!file) {
    DEBUG_PRINT("[SD] Failed to open file for writing");
    return "";
  }

  file.write(fb->buf, fb->len);
  file.close();

  DEBUG_PRINTF("[SD] Photo saved: %s (%d bytes)\n", filename.c_str(), fb->len);
  return filename;
}

// =================== WEBSOCKET FUNCTIONS ===================

void setupWebSocket() {
  snprintf(websocketPath, sizeof(websocketPath), WEBSOCKET_PATH_TEMPLATE, SERIAL_NUMBER, DEVICE_TYPE);
  webSocket.beginSSL(WEBSOCKET_HOST, WEBSOCKET_PORT, websocketPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(state.reconnectInterval);
  webSocket.enableHeartbeat(15000, 3000, 2);
  DEBUG_PRINTF("[WebSocket] Connecting to: wss://%s:%d%s\n", WEBSOCKET_HOST, WEBSOCKET_PORT, websocketPath);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      DEBUG_PRINT("[WebSocket] Disconnected");
      state.isConnected = false;
      state.namespaceConnected = false;
      state.cameraOnlineSent = false;
      state.streamActive = false;
      state.reconnectAttempts++;
      state.reconnectInterval = min(state.reconnectInterval * 2, 60000UL);
      break;
      
    case WStype_CONNECTED:
      DEBUG_PRINTF("[WebSocket] Connected: %s\n", payload);
      state.isConnected = true;
      state.namespaceConnected = false;
      state.cameraOnlineSent = false;
      state.reconnectAttempts = 0;
      state.reconnectInterval = RECONNECT_BASE_INTERVAL;
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(String((char*)payload));
      break;
      
    case WStype_ERROR:
      DEBUG_PRINTF("[WebSocket] Error: %s\n", payload);
      break;
      
    default:
      break;
  }
}

void handleWebSocketMessage(const String& message) {
  if (message.length() < 1) return;
  
  char engineIOType = message.charAt(0);
  
  switch(engineIOType) {
    case '0': // Engine.IO OPEN
      DEBUG_PRINT("[Engine.IO] OPEN");
      delay(2000);
      joinCameraNamespace();
      break;
      
    case '2': // Engine.IO PING
      DEBUG_PRINT("[Engine.IO] PING");
      webSocket.sendTXT("3");
      break;
      
    case '3': // Engine.IO PONG
      DEBUG_PRINT("[Engine.IO] PONG");
      break;
      
    case '4': // Socket.IO message
      handleSocketIOMessage(message.substring(1));
      break;
  }
}

void joinCameraNamespace() {
  DEBUG_PRINT("[Socket.IO] Joining /camera namespace...");
  webSocket.sendTXT("40/camera,");
}

void handleSocketIOMessage(const String& socketIOData) {
  if (socketIOData.length() < 1) return;
  
  char socketIOType = socketIOData.charAt(0);
  
  switch(socketIOType) {
    case '0': // Connected to namespace
      if (socketIOData.indexOf("/camera") != -1) {
        DEBUG_PRINT("[Socket.IO] Connected to /camera namespace");
        state.namespaceConnected = true;
        delay(3000);
        sendCameraOnline();
      }
      break;
      
    case '2': // Event
      parseSocketIOEvent(socketIOData.substring(1));
      break;
      
    case '4': // Error
      DEBUG_PRINTF("[Socket.IO] ERROR: %s\n", socketIOData.c_str());
      if (!state.namespaceConnected) {
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
    
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, jsonData);
    
    if (!err) {
      handleSocketIOEvent(eventName, doc);
    }
  }
}

void handleSocketIOEvent(const String& eventName, JsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    String requestId = doc["requestId"];
    
    DEBUG_PRINTF("[Command] Received: %s (ID: %s)\n", action.c_str(), requestId.c_str());
    
    if (action == "capture") {
      handleCaptureCommand(doc, requestId);
    }
    else if (action == "setFaceDetection") {
      state.faceDetectionEnabled = doc["params"]["enabled"] | false;
      saveConfiguration();
      sendCommandResponse(requestId, true, action, 
                         "Face detection " + String(state.faceDetectionEnabled ? "enabled" : "disabled"));
    }
    else if (action == "setMotionDetection") {
      state.motionDetectionEnabled = doc["params"]["enabled"] | false;
      saveConfiguration();
      sendCommandResponse(requestId, true, action,
                         "Motion detection " + String(state.motionDetectionEnabled ? "enabled" : "disabled"));
    }
    else if (action == "setStreamKey") {
      muxStreamKey = doc["params"]["streamKey"].as<String>();
      DEBUG_PRINTF("[Mux] Received stream key: %s\n", muxStreamKey.c_str());
      sendCommandResponse(requestId, true, action, "Stream key updated");
    }
    else if (action == "startRTMPStream") {
      rtmpUrl = doc["params"]["rtmpUrl"].as<String>();
      state.rtmpStreamActive = true;
      state.streamActive = true;
      DEBUG_PRINTF("[RTMP] Starting stream to: %s\n", rtmpUrl.c_str());
      sendCommandResponse(requestId, true, action, "RTMP stream started");
    }
    else if (action == "stopRTMPStream") {
      state.rtmpStreamActive = false;
      state.streamActive = false;
      sendCommandResponse(requestId, true, action, "RTMP stream stopped");
    }
    else if (action == "setResolution") {
      handleResolutionCommand(doc, requestId);
    }
    else if (action == "setQuality") {
      handleQualityCommand(doc, requestId);
    }
    else if (action == "reboot") {
      sendCommandResponse(requestId, true, action, "Rebooting device");
      delay(1000);
      ESP.restart();
    }
    else {
      sendCommandResponse(requestId, false, action, "Unknown command: " + action);
    }
  }
}

void handleCaptureCommand(JsonDocument& doc, const String& requestId) {
  bool saveToSD = doc["params"]["saveToSD"] | true;
  int quality = doc["params"]["quality"] | settings.quality;
  bool detectFaces = doc["params"]["detectFaces"] | state.faceDetectionEnabled;
  
  // Temporarily set quality
  sensor_t * s = esp_camera_sensor_get();
  int originalQuality = settings.quality;
  if (s && quality != originalQuality) {
    s->set_quality(s, quality);
  }
  
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    sendCommandResponse(requestId, false, "capture", "Failed to capture image");
    return;
  }
  
  String filename = "";
  String faceData = "";
  
  // Process face detection
  if (detectFaces) {
    faceData = processFaceDetection(fb);
  }
  
  // Save to SD card
  if (saveToSD && state.sdCardAvailable) {
    filename = savePhotoToSD(fb);
  }
  
  // Prepare response
  StaticJsonDocument<512> responseData;
  responseData["filename"] = filename;
  responseData["size"] = fb->len;
  responseData["quality"] = quality;
  responseData["resolution"] = settings.frameSize;
  
  if (faceData.length() > 0) {
    StaticJsonDocument<512> faceDoc;
    if (deserializeJson(faceDoc, faceData) == DeserializationError::Ok) {
      responseData["faces"] = faceDoc;
    }
  }
  
  String responseStr;
  serializeJson(responseData, responseStr);
  
  esp_camera_fb_return(fb);
  
  // Restore original quality
  if (s && quality != originalQuality) {
    s->set_quality(s, originalQuality);
  }
  
  sendCommandResponse(requestId, true, "capture", "Photo captured successfully", responseStr);
}

void handleResolutionCommand(JsonDocument& doc, const String& requestId) {
  framesize_t frameSize = (framesize_t)doc["params"]["size"].as<int>();
  sensor_t * s = esp_camera_sensor_get();
  
  if (s && s->set_framesize(s, frameSize) == 0) {
    settings.frameSize = frameSize;
    saveConfiguration();
    sendCommandResponse(requestId, true, "setResolution", "Resolution updated to " + String(frameSize));
  } else {
    sendCommandResponse(requestId, false, "setResolution", "Failed to set resolution");
  }
}

void handleQualityCommand(JsonDocument& doc, const String& requestId) {
  int quality = doc["params"]["quality"];
  sensor_t * s = esp_camera_sensor_get();
  
  if (s && quality >= 1 && quality <= 63 && s->set_quality(s, quality) == 0) {
    settings.quality = quality;
    saveConfiguration();
    sendCommandResponse(requestId, true, "setQuality", "Quality updated to " + String(quality));
  } else {
    sendCommandResponse(requestId, false, "setQuality", "Failed to set quality");
  }
}

void sendSocketIOEvent(const char* eventName, const String& jsonData) {
  if (!state.namespaceConnected) return;
  
  String eventPayload = "42/camera,[\"";
  eventPayload += eventName;
  eventPayload += "\",";
  eventPayload += jsonData;
  eventPayload += "]";
  
  webSocket.sendTXT(eventPayload);
}

void sendCommandResponse(const String& requestId, bool success, const String& action, const String& message, const String& additionalData = "") {
  StaticJsonDocument<512> response;
  response["requestId"] = requestId;
  response["success"] = success;
  response["serialNumber"] = SERIAL_NUMBER;
  
  if (additionalData.length() > 0) {
    StaticJsonDocument<512> dataDoc;
    if (deserializeJson(dataDoc, additionalData) == DeserializationError::Ok) {
      response["result"] = dataDoc;
    } else {
      response["result"]["message"] = message;
    }
  } else {
    response["result"]["message"] = message;
  }
  
  String jsonString;
  serializeJson(response, jsonString);
  sendSocketIOEvent("command_response", jsonString);
}

void sendCameraOnline() {
  if (!state.namespaceConnected || state.cameraOnlineSent) return;
  
  StaticJsonDocument<512> doc;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["sd_card"] = state.sdCardAvailable;
  
  JsonObject capabilities = doc.createNestedObject("capabilities");
  capabilities["streaming"] = true;
  capabilities["photo_capture"] = true;
  capabilities["face_detection"] = true;
  capabilities["motion_detection"] = true;
  capabilities["rtmp_streaming"] = true;
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("camera_online", jsonString);
  state.cameraOnlineSent = true;
  
  DEBUG_PRINT("[Status] Camera online notification sent");
}

void sendCameraStatus() {
  if (!state.namespaceConnected) return;
  
  StaticJsonDocument<512> doc;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["status"] = state.streamActive ? "streaming" : "idle";
  doc["streamActive"] = state.streamActive;
  doc["rtmpActive"] = state.rtmpStreamActive;
  doc["resolution"] = settings.frameSize;
  doc["quality"] = settings.quality;
  doc["faceDetection"] = state.faceDetectionEnabled;
  doc["motionDetection"] = state.motionDetectionEnabled;
  doc["clients"] = state.streamClients;
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("camera_status", jsonString);
}

// =================== HTTP SERVER ===================

void setupHttpServer() {
  // Configuration page
  httpServer.on("/", HTTP_GET, []() {
    char html[4000];
    snprintf(html, sizeof(html), CONFIG_HTML, SERIAL_NUMBER, FIRMWARE_VERSION);
    httpServer.send(200, "text/html", html);
  });
  
  // Save configuration
  httpServer.on("/save_config", HTTP_POST, []() {
    String ssid = httpServer.arg("ssid");
    String password = httpServer.arg("password");
    int quality = httpServer.arg("quality").toInt();
    bool faceDetection = httpServer.hasArg("faceDetection");
    bool motionDetection = httpServer.hasArg("motionDetection");
    
    writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
    writeEEPROMString(WIFI_PASS_ADDR, password, 100);
    
    settings.quality = quality;
    state.faceDetectionEnabled = faceDetection;
    state.motionDetectionEnabled = motionDetection;
    saveConfiguration();
    
    httpServer.send(200, "text/html", 
      "<html><body style='font-family:Arial;text-align:center;padding:50px'>"
      "<h1>✅ Configuration Saved</h1>"
      "<p>Device will restart and connect to: <strong>" + ssid + "</strong></p>"
      "<p>Redirecting in 3 seconds...</p>"
      "<script>setTimeout(function(){window.location.href='/';}, 3000);</script>"
      "</body></html>");
    
    delay(2000);
    ESP.restart();
  });
  
  // Status API
  httpServer.on("/status", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["device"] = SERIAL_NUMBER;
    doc["firmware"] = FIRMWARE_VERSION;
    doc["status"] = state.streamActive ? "streaming" : "idle";
    doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"] = WiFi.localIP().toString();
    doc["wifi"]["rssi"] = WiFi.RSSI();
    doc["memory"]["free"] = ESP.getFreeHeap();
    doc["memory"]["total"] = ESP.getHeapSize();
    doc["uptime"] = millis() / 1000;
    doc["features"]["face_detection"] = state.faceDetectionEnabled;
    doc["features"]["motion_detection"] = state.motionDetectionEnabled;
    doc["features"]["sd_card"] = state.sdCardAvailable;
    
    String response;
    serializeJson(doc, response);
    
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", response);
  });
  
  // Stream endpoint
  httpServer.on("/stream", HTTP_GET, handleMJPEGStream);
  
  // Photo endpoint
  httpServer.on("/photo", HTTP_GET, handlePhotoDownload);
  
  // Thumbnail endpoint
  httpServer.on("/thumbnail", HTTP_GET, handleThumbnailDownload);
  
  // CORS preflight
  httpServer.on("/status", HTTP_OPTIONS, []() {
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    httpServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    httpServer.send(200);
  });
  
  httpServer.begin();
  DEBUG_PRINT("[HTTP] Server started on port 80");
}

void handleMJPEGStream() {
  WiFiClient client = httpServer.client();
  
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  
  client.print(response);
  
  state.streamClients++;
  state.streamActive = true;
  
  while (client.connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      DEBUG_PRINT("[Stream] Failed to capture frame");
      break;
    }
    
    String header = "--frame\r\n";
    header += "Content-Type: image/jpeg\r\n";
    header += "Content-Length: " + String(fb->len) + "\r\n\r\n";
    
    client.print(header);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    
    esp_camera_fb_return(fb);
    
    if (!client.connected()) break;
    delay(100);
  }
  
  state.streamClients--;
  if (state.streamClients <= 0) {
    state.streamActive = false;
    state.streamClients = 0;
  }
  
  client.stop();
}

void handlePhotoDownload() {
  String filename = httpServer.arg("filename");
  if (filename.length() == 0 || !state.sdCardAvailable) {
    httpServer.send(404, "text/plain", "Photo not found");
    return;
  }
  
  File file = SD_MMC.open("/" + filename);
  if (!file) {
    httpServer.send(404, "text/plain", "Photo not found");
    return;
  }
  
  httpServer.streamFile(file, "image/jpeg");
  file.close();
}

void handleThumbnailDownload() {
  // For simplicity, return the same image
  // In production, you might want to generate actual thumbnails
  handlePhotoDownload();
}

// =================== MOTION DETECTION ===================

void checkMotion() {
  if (!state.motionDetectionEnabled) return;

  static bool lastMotionState = false;
  static unsigned long lastMotionTime = 0;
  
  bool motionState = digitalRead(MOTION_SENSOR_PIN);

  if (motionState && !lastMotionState && millis() - lastMotionTime > 5000) {
    DEBUG_PRINT("[Motion] Detected!");
    lastMotionTime = millis();
    
    // Capture photo if SD card available
    if (state.sdCardAvailable) {
      camera_fb_t * fb = esp_camera_fb_get();
      if (fb) {
        String filename = savePhotoToSD(fb, true);
        
        // Process face detection for motion-triggered photo
        String faceData = "";
        if (state.faceDetectionEnabled) {
          faceData = processFaceDetection(fb);
        }
        
        esp_camera_fb_return(fb);
        
        // Send notification
        if (state.namespaceConnected) {
          StaticJsonDocument<512> doc;
          doc["serialNumber"] = SERIAL_NUMBER;
          doc["type"] = "motion";
          doc["timestamp"] = millis();
          doc["filename"] = filename;
          
          if (faceData.length() > 0) {
            StaticJsonDocument<512> faceDoc;
            if (deserializeJson(faceDoc, faceData) == DeserializationError::Ok) {
              doc["faces"] = faceDoc;
            }
          }
          
          String jsonString;
          serializeJson(doc, jsonString);
          sendSocketIOEvent("motion_detected", jsonString);
        }
      }
    }
  }

  lastMotionState = motionState;
}

// =================== MAIN FUNCTIONS ===================

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  
  DEBUG_PRINT("\n==============================");
  DEBUG_PRINT("🎥 ESP32-CAM Advanced v1.4.0");
  DEBUG_PRINT("==============================");
  DEBUG_PRINTF("Device: %s\n", SERIAL_NUMBER);
  DEBUG_PRINTF("Features: Face Detection, Motion Detection, RTMP\n");
  DEBUG_PRINTF("Free Heap: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize hardware
  pinMode(MOTION_SENSOR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize EEPROM and load configuration
  initEEPROM();
  loadConfiguration();
  
  // Load WiFi credentials
  wifiSSID = readEEPROMString(WIFI_SSID_ADDR, 100);
  wifiPassword = readEEPROMString(WIFI_PASS_ADDR, 100);
  
  // Initialize camera
  if (!initCamera()) {
    DEBUG_PRINT("[ERROR] Camera initialization failed. Restarting...");
    delay(3000);
    ESP.restart();
  }
  
  // Initialize SD card
  initSDCard();
  
  // Connect to WiFi or start hotspot
  if (wifiSSID.length() > 0 && connectToWiFi(wifiSSID, wifiPassword)) {
    setupWebSocket();
  } else {
    startHotspot();
    udp.begin(UDP_PORT);
    DEBUG_PRINTF("[UDP] Listening on port %d\n", UDP_PORT);
  }
  
  // Start HTTP server
  setupHttpServer();
  
  // Initialize timing
  state.lastStatusUpdate = millis();
  state.lastWatchdog = millis();
  
  DEBUG_PRINT("[Setup] Initialization complete");
  DEBUG_PRINTF("[Memory] Free heap: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
  unsigned long currentTime = millis();
  
  // Handle WebSocket
  if (!state.isHotspotMode) {
    webSocket.loop();
  }
  
  // Handle HTTP server
  httpServer.handleClient();
  
  // Handle UDP in hotspot mode
  if (state.isHotspotMode) {
    handleUDP();
  } else {
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
      DEBUG_PRINT("[WiFi] Connection lost, attempting reconnection...");
      if (!connectToWiFi(wifiSSID, wifiPassword)) {
        DEBUG_PRINT("[WiFi] Reconnection failed, starting hotspot...");
        startHotspot();
        udp.begin(UDP_PORT);
      }
      return;
    }
    
    // Send periodic status updates
    if (state.isConnected && state.namespaceConnected && state.cameraOnlineSent) {
      if (currentTime - state.lastStatusUpdate > STATUS_INTERVAL) {
        sendCameraStatus();
        state.lastStatusUpdate = currentTime;
        DEBUG_PRINTF("[Status] Free heap: %d bytes\n", ESP.getFreeHeap());
      }
    } else if (currentTime - state.lastStatusUpdate > 30000) {
      DEBUG_PRINTF("[Status] Connection state - Connected: %s, Namespace: %s, Online sent: %s\n", 
                   state.isConnected ? "YES" : "NO",
                   state.namespaceConnected ? "YES" : "NO", 
                   state.cameraOnlineSent ? "YES" : "NO");
      state.lastStatusUpdate = currentTime;
    }
  }
  
  // Check motion detection
  checkMotion();
  
  // Watchdog timer
  if (currentTime - state.lastWatchdog > WATCHDOG_TIMEOUT) {
    DEBUG_PRINT("[Watchdog] Timeout reached, restarting...");
    ESP.restart();
  }
  
  // Reset watchdog if connected
  if (state.isConnected || state.isHotspotMode) {
    state.lastWatchdog = currentTime;
  }
  
  // Small delay to prevent watchdog issues
  delay(10);
}