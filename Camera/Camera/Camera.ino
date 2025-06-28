/**************************************************************
 * ESP32-CAM with Socket.IO Integration - FIXED VERSION
 * Version: v1.2 - HTTP Routes Fixed
 * Features: MJPEG streaming, WebSocket control, SD card storage, motion detection
 **************************************************************/

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"
#include "FS.h"
#include "driver/rtc_io.h"

// Camera model
#define CAMERA_MODEL_AI_THINKER

// Device identification
#define SERIAL_NUMBER "SERL12ESP32CAM001"
#define DEVICE_TYPE "CAMERA"
#define FIRMWARE_VERSION "1.2.0"

// WiFi credentials
const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";

// Server configuration
String WEBSOCKET_HOST = "192.168.1.5";
uint16_t WEBSOCKET_PORT = 7777;
// FIXED: Use /camera namespace in connection path
String WEBSOCKET_PATH = "/socket.io/?EIO=4&transport=websocket&serialNumber=" + String(SERIAL_NUMBER) + "&deviceType=" + String(DEVICE_TYPE);

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
bool isConnected = false;
bool namespaceConnected = false;
int reconnectAttempts = 0;
bool cameraOnlineSent = false; // FIXED: Track if camera_online was sent

// Camera settings
int currentQuality = 10;
framesize_t currentFrameSize = FRAMESIZE_VGA;

// FIXED: Adjusted timing intervals
const unsigned long PING_INTERVAL = 20000; // Increased to 20s to match server
const unsigned long STATUS_INTERVAL = 30000; // Status every 30s instead of 10s
const unsigned long RECONNECT_BASE_INTERVAL = 10000; // Increased base interval
unsigned long reconnectInterval = RECONNECT_BASE_INTERVAL;

/**************************************************************
 * WEBSOCKET FUNCTIONS - FIXED
 **************************************************************/

void joinCameraNamespace() {
  Serial.println("[Socket.IO] Attempting to join /camera namespace...");
  String namespaceJoin = "40/camera,";
  webSocket.sendTXT(namespaceJoin);
  Serial.println("[Socket.IO] Sent namespace join request: " + namespaceJoin);
}

void sendSocketIOEvent(String eventName, String jsonData) {
  if (!namespaceConnected) {
    Serial.println("[Socket.IO] WARNING: Namespace not connected, cannot send events");
    return;
  }
  
  String eventPayload = "42/camera,[\"" + eventName + "\"," + jsonData + "]";
  webSocket.sendTXT(eventPayload);
  Serial.println("[Socket.IO] Sent event '" + eventName + "'");
  Serial.println("[Socket.IO] Payload length: " + String(eventPayload.length()));
}

void sendCameraOnline() {
  if (!namespaceConnected || cameraOnlineSent) {
    Serial.println("[Socket.IO] Cannot send camera_online: namespace not connected or already sent");
    return;
  }
  
  // FIXED: Simplified JSON to avoid memory issues
  DynamicJsonDocument doc(1024);
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
  Serial.println("[CAMERA] ✅ Online message sent successfully!");
}

void sendCameraStatus() {
  if (!namespaceConnected) return;
  
  // FIXED: Simplified status to essential info only
  DynamicJsonDocument doc(512);
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

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char engineIOType = message.charAt(0);
  
  switch(engineIOType) {
    case '0': // Engine.IO OPEN
      Serial.println("[Engine.IO] OPEN - Session established");
      if (message.length() > 1) {
        String sessionData = message.substring(1);
        Serial.println("[Engine.IO] Session data: " + sessionData);
        // FIXED: Wait longer before joining namespace
        delay(2000);
        joinCameraNamespace();
      }
      break;
      
    case '2': // Engine.IO PING
      Serial.println("[Engine.IO] PING received - sending PONG");
      webSocket.sendTXT("3"); // Send PONG response
      break;
      
    case '3': // Engine.IO PONG
      Serial.println("[Engine.IO] PONG received");
      break;
      
    case '4': // Engine.IO MESSAGE
      if (message.length() > 1) {
        String socketIOData = message.substring(1);
        handleSocketIOMessage(socketIOData);
      }
      break;
      
    default:
      Serial.printf("[Engine.IO] Unknown packet type: %c\n", engineIOType);
      break;
  }
}

// FIXED: Removed automatic ping sending to prevent disconnections
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WebSocket] Disconnected from server. Attempts: %d\n", reconnectAttempts);
      isConnected = false;
      namespaceConnected = false;
      cameraOnlineSent = false; // FIXED: Reset online status
      reconnectInterval = min(reconnectInterval * 2, 60000UL); // Max 1 minute
      reconnectAttempts++;
      Serial.printf("[WebSocket] Next reconnect attempt in %lu ms\n", reconnectInterval);
      break;
      
    case WStype_CONNECTED:
      Serial.printf("[WebSocket] Connected to server: %s\n", payload);
      Serial.println("[WebSocket] ✅ WebSocket connection established");
      isConnected = true;
      namespaceConnected = false;
      cameraOnlineSent = false; // FIXED: Reset online status
      reconnectAttempts = 0;
      reconnectInterval = RECONNECT_BASE_INTERVAL;
      // FIXED: Don't join namespace immediately, wait for Engine.IO OPEN
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[WebSocket] Message received: %s\n", message.c_str());
        handleWebSocketMessage(message);
        break;
      }
      
    case WStype_PING:
      Serial.println("[WebSocket] Server ping received");
      break;
      
    case WStype_PONG:
      Serial.println("[WebSocket] Pong received from server");
      break;
      
    case WStype_ERROR:
      Serial.printf("[WebSocket] Error: %s\n", payload ? (char*)payload : "Unknown");
      break;
  }
}

void handleSocketIOMessage(String socketIOData) {
  if (socketIOData.length() < 1) return;
  
  char socketIOType = socketIOData.charAt(0);
  
  switch(socketIOType) {
    case '0': // Socket.IO CONNECT
      if (socketIOData.indexOf("/camera") != -1) {
        Serial.println("[Socket.IO] ✅ Connected to /camera namespace!");
        namespaceConnected = true;
        reconnectAttempts = 0;
        reconnectInterval = RECONNECT_BASE_INTERVAL;
        // FIXED: Wait longer before sending camera_online
        delay(3000);
        sendCameraOnline();
        Serial.printf("[Heap] Free heap: %d bytes\n", ESP.getFreeHeap());
      }
      break;
      
    case '2': // Socket.IO EVENT
      parseSocketIOEvent(socketIOData.substring(1));
      break;
      
    case '4': // Socket.IO ERROR
      Serial.println("[Socket.IO] ERROR received: " + socketIOData);
      if (!namespaceConnected) {
        Serial.println("[Socket.IO] Retrying namespace connection...");
        delay(3000);
        joinCameraNamespace();
      }
      break;
      
    default:
      Serial.printf("[Socket.IO] Unknown packet type: %c\n", socketIOType);
      break;
  }
}

void parseSocketIOEvent(String eventData) {
  if (eventData.startsWith("/camera,")) {
    eventData = eventData.substring(8);
  }
  
  int firstBracket = eventData.indexOf('[');
  if (firstBracket == -1) return;
  
  int firstQuote = eventData.indexOf('"', firstBracket);
  if (firstQuote == -1) return;
  
  int secondQuote = eventData.indexOf('"', firstQuote + 1);
  if (secondQuote == -1) return;
  
  String eventName = eventData.substring(firstQuote + 1, secondQuote);
  Serial.println("[Socket.IO] Event name: " + eventName);
  
  int jsonStart = eventData.indexOf('{');
  if (jsonStart != -1) {
    String jsonData = eventData.substring(jsonStart, eventData.lastIndexOf('}') + 1);
    Serial.println("[Socket.IO] JSON data: " + jsonData);
    
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, jsonData);
    
    if (!err) {
      handleCommand(eventName, doc);
    } else {
      Serial.println("[Socket.IO] JSON parse error: " + String(err.c_str()));
    }
  }
}

void handleCommand(String eventName, DynamicJsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    
    DynamicJsonDocument response(512);
    response["success"] = false;
    response["serialNumber"] = SERIAL_NUMBER;
    response["commandId"] = action;
    response["timestamp"] = millis();
    
    if (action == "capture") {
      bool saveToSD = doc["params"]["saveToSD"] | true;
      int quality = doc["params"]["quality"] | 10;
      
      sensor_t * s = esp_camera_sensor_get();
      s->set_quality(s, quality);
      
      String filename = saveToSD ? savePhotoToSD(false) : "";
      response["success"] = filename != "" || !saveToSD;
      response["filename"] = filename;
      response["size"] = filename != "" ? SD_MMC.open(filename).size() : 0;
      response["message"] = filename != "" ? "Photo captured" : "Capture failed";
    }
    else if (action == "setResolution") {
      framesize_t frameSize = (framesize_t)doc["params"]["size"].as<int>();
      sensor_t * s = esp_camera_sensor_get();
      s->set_framesize(s, frameSize);
      currentFrameSize = frameSize;
      response["success"] = true;
      response["message"] = "Resolution updated";
    }
    else if (action == "setQuality") {
      int quality = doc["params"]["quality"];
      sensor_t * s = esp_camera_sensor_get();
      s->set_quality(s, quality);
      currentQuality = quality;
      response["success"] = true;
      response["message"] = "Quality updated";
    }
    else if (action == "toggleMotion") {
      motionDetectionEnabled = doc["params"]["enabled"] | false;
      response["success"] = true;
      response["message"] = "Motion detection " + String(motionDetectionEnabled ? "enabled" : "disabled");
    }
    else {
      response["message"] = "Unknown command: " + action;
    }
    
    String jsonString;
    serializeJson(response, jsonString);
    sendSocketIOEvent("command_response", jsonString);
  }
}

/**************************************************************
 * CAMERA AND SD CARD FUNCTIONS
 **************************************************************/

#define MOTION_SENSOR_PIN 13

bool initSDCard() {
  Serial.println("[SD] Initializing SD card...");
  if (!SD_MMC.begin()) {
    Serial.println("[SD] Initialization failed!");
    sdCardAvailable = false;
    return false;
  }
  Serial.println("[SD] Initialization done.");
  sdCardAvailable = true;
  return true;
}

bool initCamera() {
  Serial.println("[Camera] Initializing camera...");
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
  Serial.println("[Camera] Initialization done.");
  return true;
}

String savePhotoToSD(bool triggeredByMotion) {
  if (!sdCardAvailable) {
    Serial.println("[SD] Cannot save photo: SD card not available");
    return "";
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Camera] Failed to capture photo");
    return "";
  }

  String filename = triggeredByMotion ? "/motion_" : "/photo_";
  filename += String(millis()) + ".jpg";

  File file = SD_MMC.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("[SD] Failed to open file for writing");
    esp_camera_fb_return(fb);
    return "";
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  Serial.println("[SD] Photo saved: " + filename);
  return filename;
}

void setupWebSocket() {
  String wsUrl = "ws://" + WEBSOCKET_HOST + ":" + String(WEBSOCKET_PORT) + WEBSOCKET_PATH;
  webSocket.begin(WEBSOCKET_HOST, WEBSOCKET_PORT, WEBSOCKET_PATH);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(reconnectInterval);
  Serial.println("[WebSocket] Connecting to: " + wsUrl);
}

// **FIXED: HTTP Server setup with standard route handling**
void setupHttpServer() {
  // MJPEG Stream endpoint
  httpServer.on("/stream", HTTP_GET, []() {
    streamClients++;
    streamActive = true;
    sendCameraStatus();
    
    // Set MJPEG headers
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    httpServer.sendHeader("Pragma", "no-cache");
    httpServer.sendHeader("Expires", "0");
    httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer.send(200, "multipart/x-mixed-replace; boundary=frame", "");
    
    Serial.println("[HTTP] MJPEG stream started");
    
    // Stream frames
    WiFiClient client = httpServer.client();
    while (client.connected() && streamActive) {
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("[Camera] Frame buffer failed");
        break;
      }
      
      client.print("--frame\r\n");
      client.print("Content-Type: image/jpeg\r\n");
      client.printf("Content-Length: %u\r\n\r\n", fb->len);
      client.write(fb->buf, fb->len);
      client.print("\r\n");
      
      esp_camera_fb_return(fb);
      delay(100); // ~10 FPS
    }
    
    streamClients--;
    if (streamClients <= 0) {
      streamActive = false;
      streamClients = 0;
    }
    Serial.println("[HTTP] Stream ended");
  });

  // Camera status endpoint
  httpServer.on("/status", HTTP_GET, []() {
    DynamicJsonDocument doc(512);
    doc["status"] = streamActive ? "streaming" : "idle";
    doc["streamActive"] = streamActive;
    doc["resolution"] = currentFrameSize;
    doc["quality"] = currentQuality;
    doc["fps"] = streamActive ? 10 : 0;
    doc["clients"] = streamClients;
    doc["uptime"] = millis() / 1000;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["motionDetection"] = motionDetectionEnabled;
    doc["sdCardAvailable"] = sdCardAvailable;
    doc["timestamp"] = millis();
    
    String response;
    serializeJson(doc, response);
    
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", response);
  });

  // Camera command endpoint
  httpServer.on("/command", HTTP_POST, []() {
    String body = httpServer.arg("plain");
    DynamicJsonDocument doc(512);
    
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
      httpServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    String action = doc["action"];
    JsonObject params = doc["params"];
    
    DynamicJsonDocument response(512);
    response["success"] = false;
    response["action"] = action;
    response["timestamp"] = millis();
    
    if (action == "capture") {
      bool saveToSD = params["saveToSD"] | true;
      int quality = params["quality"] | 10;
      
      sensor_t * s = esp_camera_sensor_get();
      s->set_quality(s, quality);
      
      if (saveToSD) {
        String filename = savePhotoToSD(false);
        if (filename != "") {
          response["success"] = true;
          response["filename"] = filename;
          response["size"] = SD_MMC.open(filename).size();
          response["message"] = "Photo captured and saved to SD";
        } else {
          response["message"] = "Failed to save photo to SD";
        }
      } else {
        // Just capture without saving
        camera_fb_t * fb = esp_camera_fb_get();
        if (fb) {
          response["success"] = true;
          response["filename"] = "capture_" + String(millis()) + ".jpg";
          response["size"] = fb->len;
          response["message"] = "Photo captured";
          esp_camera_fb_return(fb);
        } else {
          response["message"] = "Failed to capture photo";
        }
      }
    }
    else if (action == "setResolution") {
      framesize_t frameSize = (framesize_t)params["size"].as<int>();
      sensor_t * s = esp_camera_sensor_get();
      if (s->set_framesize(s, frameSize) == 0) {
        currentFrameSize = frameSize;
        response["success"] = true;
        response["message"] = "Resolution updated";
      } else {
        response["message"] = "Failed to set resolution";
      }
    }
    else if (action == "setQuality") {
      int quality = params["quality"];
      sensor_t * s = esp_camera_sensor_get();
      if (s->set_quality(s, quality) == 0) {
        currentQuality = quality;
        response["success"] = true;
        response["message"] = "Quality updated";
      } else {
        response["message"] = "Failed to set quality";
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
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", responseStr);
  });

  // Camera configuration endpoint
  httpServer.on("/config", HTTP_POST, []() {
    String body = httpServer.arg("plain");
    DynamicJsonDocument doc(512);
    
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
      httpServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    DynamicJsonDocument response(512);
    response["success"] = true;
    response["message"] = "Configuration updated";
    
    // Update camera settings
    if (doc.containsKey("resolution")) {
      String resStr = doc["resolution"];
      framesize_t newRes = FRAMESIZE_VGA; // default
      
      if (resStr == "QVGA") newRes = FRAMESIZE_QVGA;
      else if (resStr == "CIF") newRes = FRAMESIZE_CIF;
      else if (resStr == "VGA") newRes = FRAMESIZE_VGA;
      else if (resStr == "SVGA") newRes = FRAMESIZE_SVGA;
      else if (resStr == "XGA") newRes = FRAMESIZE_XGA;
      else if (resStr == "SXGA") newRes = FRAMESIZE_SXGA;
      else if (resStr == "UXGA") newRes = FRAMESIZE_UXGA;
      
      sensor_t * s = esp_camera_sensor_get();
      s->set_framesize(s, newRes);
      currentFrameSize = newRes;
    }
    
    if (doc.containsKey("quality")) {
      int quality = doc["quality"];
      sensor_t * s = esp_camera_sensor_get();
      s->set_quality(s, quality);
      currentQuality = quality;
    }
    
    if (doc.containsKey("motionDetection")) {
      motionDetectionEnabled = doc["motionDetection"];
    }
    
    String responseStr;
    serializeJson(response, responseStr);
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", responseStr);
  });

  // Get photos list endpoint
  httpServer.on("/photos", HTTP_GET, []() {
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
          photo["timestamp"] = file.getLastWrite() * 1000; // Convert to milliseconds
          count++;
        }
        file = root.openNextFile();
      }
    }
    
    doc["total"] = photosArray.size();
    
    String response;
    serializeJson(doc, response);
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", response);
  });

  // **FIXED: Handle photo downloads with standard routes**
  httpServer.on("/photo", HTTP_GET, []() {
    if (!httpServer.hasArg("filename")) {
      httpServer.send(400, "text/plain", "Filename parameter required");
      return;
    }
    
    String filename = httpServer.arg("filename");
    
    if (!sdCardAvailable) {
      httpServer.send(404, "text/plain", "SD card not available");
      return;
    }
    
    String filepath = "/" + filename;
    if (!SD_MMC.exists(filepath)) {
      httpServer.send(404, "text/plain", "File not found");
      return;
    }
    
    File file = SD_MMC.open(filepath);
    if (!file) {
      httpServer.send(500, "text/plain", "Failed to open file");
      return;
    }
    
    httpServer.sendHeader("Content-Type", "image/jpeg");
    httpServer.sendHeader("Content-Disposition", "attachment; filename=" + filename);
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.streamFile(file, "image/jpeg");
    file.close();
  });

  // **FIXED: Handle thumbnails with standard routes**
  httpServer.on("/thumbnail", HTTP_GET, []() {
    if (!httpServer.hasArg("filename")) {
      httpServer.send(400, "text/plain", "Filename parameter required");
      return;
    }
    
    String filename = httpServer.arg("filename");
    
    if (!sdCardAvailable) {
      httpServer.send(404, "text/plain", "SD card not available");
      return;
    }
    
    // For now, serve the same image as thumbnail
    // In a real implementation, you'd generate a smaller version
    String filepath = "/" + filename;
    if (!SD_MMC.exists(filepath)) {
      httpServer.send(404, "text/plain", "File not found");
      return;
    }
    
    File file = SD_MMC.open(filepath);
    if (!file) {
      httpServer.send(500, "text/plain", "Failed to open file");
      return;
    }
    
    httpServer.sendHeader("Content-Type", "image/jpeg");
    httpServer.sendHeader("Content-Disposition", "inline; filename=thumb_" + filename);
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.streamFile(file, "image/jpeg");
    file.close();
  });

  // Handle preflight OPTIONS requests
  httpServer.on("/command", HTTP_OPTIONS, []() {
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    httpServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    httpServer.send(200);
  });

  httpServer.on("/config", HTTP_OPTIONS, []() {
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    httpServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    httpServer.send(200);
  });

  httpServer.onNotFound([]() {
    httpServer.send(404, "text/plain", "Not found");
  });

  httpServer.begin();
  Serial.println("[HTTP] Server started with all camera endpoints");
}

void checkMotion() {
  if (!motionDetectionEnabled) return;

  static bool lastMotionState = false;
  bool motionState = digitalRead(MOTION_SENSOR_PIN);

  if (motionState && !lastMotionState) {
    Serial.println("[Motion] Detected!");
    if (sdCardAvailable) {
      String filename = savePhotoToSD(true);
      if (filename != "" && namespaceConnected) {
        DynamicJsonDocument doc(256);
        doc["serialNumber"] = SERIAL_NUMBER;
        doc["filename"] = filename;
        doc["timestamp"] = millis();
        String jsonString;
        serializeJson(doc, jsonString);
        sendSocketIOEvent("motion_detected", jsonString);
      }
    }
  }

  lastMotionState = motionState;
}

/**************************************************************
 * SETUP AND LOOP - FIXED
 **************************************************************/

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("\n=== ESP32-CAM v1.2 - FIXED Camera Socket.IO ===");
  Serial.println("Device Serial: " + String(SERIAL_NUMBER));
  Serial.println("Device Type: " + String(DEVICE_TYPE));
  
  pinMode(MOTION_SENSOR_PIN, INPUT);
  
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 30000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connection failed. Restarting...");
    ESP.restart();
  }
  Serial.println(" Connected!");
  Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
  Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
  
  initSDCard();
  
  if (!initCamera()) {
    Serial.println("[ERROR] Camera initialization failed. Restarting...");
    ESP.restart();
  }
  
  setupHttpServer();
  
  Serial.println("\n[WebSocket] Starting connection...");
  setupWebSocket();
  Serial.println("[WebSocket] Connection initiated");
}

void loop() {
  webSocket.loop();
  httpServer.handleClient();
  
  // WiFi reconnection check
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connection lost, reconnecting...");
    WiFi.reconnect();
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
      delay(500);
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Reconnection failed. Restarting...");
      ESP.restart();
    }
    Serial.println("[WiFi] Reconnected: " + WiFi.localIP().toString());
    return;
  }
  
  // FIXED: Only send status and ping if properly connected and online sent
  if (isConnected && namespaceConnected && cameraOnlineSent) {
    // Send periodic status updates - less frequent
    if (millis() - lastStatusUpdate > STATUS_INTERVAL) {
      sendCameraStatus();
      lastStatusUpdate = millis();
      Serial.printf("[Status] Sent - Free heap: %d bytes, RSSI: %d dBm\n", ESP.getFreeHeap(), WiFi.RSSI());
    }
    
    // REMOVED: Automatic ping sending to prevent disconnections
    // Let the server handle ping/pong timing
    
  } else if (millis() - lastStatusUpdate > 30000) { // Log status if disconnected
    reconnectAttempts++;
    Serial.printf("[WebSocket] Status - Connected: %s, Namespace: %s, Online Sent: %s\n",
                  isConnected ? "YES" : "NO", 
                  namespaceConnected ? "YES" : "NO",
                  cameraOnlineSent ? "YES" : "NO");
    Serial.printf("[Heap] Free heap: %d bytes, RSSI: %d dBm\n", ESP.getFreeHeap(), WiFi.RSSI());
    lastStatusUpdate = millis();
  }
  
  checkMotion();
  
  delay(100); // Increased delay to reduce load
}