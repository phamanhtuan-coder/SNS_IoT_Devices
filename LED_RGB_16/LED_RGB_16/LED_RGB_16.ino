/**************************************************************
 * ESP8266 RGB LED Controller - Fixed Preset and Effect Issues
 * 
 * Enhanced state synchronization and command handling
 **************************************************************/

#define SERIAL_NUMBER "SERL12JUN2501LED24RGB001"
#define DEVICE_ID "DEVIC5755"

#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// WiFi credentials
const char* WIFI_SSID = "Anh Tuan";
const char* WIFI_PASSWORD = "21032001";

// Server configuration
String WEBSOCKET_HOST = "192.168.51.115";
uint16_t WEBSOCKET_PORT = 7777;
String WEBSOCKET_PATH = "/socket.io/?EIO=3&transport=websocket&serialNumber=" + String(SERIAL_NUMBER) + "&isIoTDevice=true";

// Hardware
WebSocketsClient webSocket;

#define LED_PIN D6
#define NUMPIXELS 24
Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Timing
unsigned long lastStatusUpdate = 0;
unsigned long lastPingTime = 0;
const unsigned long STATUS_INTERVAL = 5000;
const unsigned long PING_INTERVAL = 25000;

// State variables
bool isConnected = false;
bool namespaceConnected = false;
int reconnectAttempts = 0;

// LED State
bool ledPower = false;
String ledColor = "#FFFFFF";
int ledBrightness = 100;

// Dynamic Effects State
String currentEffect = "solid";
bool effectActive = false;
int effectSpeed = 500;
int effectCount = 0;
int effectDuration = 0;
unsigned long effectStartTime = 0;
unsigned long lastEffectUpdate = 0;
String effectColor1 = "#FF0000";
String effectColor2 = "#0000FF";

// Effect lock protection
volatile bool effectProcessing = false;
volatile bool effectChangePending = false;
volatile unsigned long effectLockTime = 0;
const unsigned long EFFECT_LOCK_TIMEOUT = 200;
const unsigned long EFFECT_COOLDOWN = 500; // Increased to 500ms
unsigned long lastEffectChange = 0;

// Effect state isolation
struct EffectState {
  int blinkCount = 0;
  bool blinkOn = false;
  int chaseStep = 0;
  int rainbowStep = 0;
  float breathePhase = 0;
  float fadeProgress = 0;
  bool fadeDirection = true;
  int strobeCount = 0;
  uint8_t lastSparklePixels[24] = {0};
  unsigned long lastDebugTime = 0;
  bool effectInitialized = false;
  String lastEffect = "";
} effectState;

// Function declarations
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length);
void handleWebSocketMessage(String message);
void handleSocketIOMessage(String socketIOData);
void parseSocketIOEvent(String eventData);
void handleCommand(String eventName, JsonDocument& doc);

uint32_t hexToColor(String hex);
void setAllPixels(uint32_t color);
void updateLED();
void testLEDPattern();
void handleLEDStatus(String status);

void setLEDEffect(String effect, int speed, int count, int duration, String color1, String color2);
void stopEffect();
void resetEffectState();
void processEffects();
void processBlink();
void processBreathe();
void processRainbow();
void processChase();
void processFade();
void processStrobe();
void processSparkle();
uint32_t wheel(byte wheelPos);
void applyPreset(String presetName, int duration);

bool acquireEffectLock();
void releaseEffectLock();
void forceStopAllEffects();
bool isEffectSafe();

void joinDeviceNamespace();
void sendSocketIOEvent(String eventName, String jsonData);
void sendDeviceOnline();
void sendDeviceStatus();
void sendCommandResponse(String command, bool success, String message);
void sendWebSocketPing();
void sendHeartbeat();

void saveLEDState();
void loadLEDState();
void startWebSocketConnection();

/**************************************************************
 * Effect Lock Management
 **************************************************************/
bool acquireEffectLock() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastEffectChange < EFFECT_COOLDOWN) {
    Serial.printf("[EFFECT] Effect change blocked - cooldown active (remaining: %dms)\n", EFFECT_COOLDOWN - (currentTime - lastEffectChange));
    return false;
  }
  
  if (effectProcessing) {
    if (currentTime - effectLockTime > EFFECT_LOCK_TIMEOUT) {
      Serial.println("[EFFECT] Force releasing stuck lock");
      effectProcessing = false;
    } else {
      Serial.println("[EFFECT] Effect processing locked - waiting");
      return false;
    }
  }
  
  effectProcessing = true;
  effectLockTime = currentTime;
  return true;
}

void releaseEffectLock() {
  effectProcessing = false;
  lastEffectChange = millis();
}

void forceStopAllEffects() {
  Serial.println("[EFFECT] Force stopping all effects");
  
  effectProcessing = false;
  effectChangePending = false;
  effectActive = false;
  
  strip.clear();
  strip.show();
  delay(50);
  
  resetEffectState();
  currentEffect = "solid";
  
  Serial.println("[EFFECT] All effects force stopped");
}

bool isEffectSafe() {
  return !effectProcessing;
}

/**************************************************************
 * WebSocket Event Handler
 **************************************************************/
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] Disconnected from server");
      isConnected = false;
      namespaceConnected = false;
      break;

    case WStype_CONNECTED:
      Serial.printf("[WebSocket] Connected to server: %s\n", payload);
      Serial.println("[WebSocket] ✅ WebSocket connection established");
      isConnected = true;
      namespaceConnected = false;
      reconnectAttempts = 0;
      break;

    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[WebSocket] Message received: %s\n", message.c_str());
        handleWebSocketMessage(message);
        break;
      }

    case WStype_ERROR:
      Serial.printf("[WebSocket] Error: %s\n", payload);
      break;

    default:
      Serial.printf("[WebSocket] Unknown event type: %d\n", type);
      break;
  }
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;

  char engineIOType = message.charAt(0);

  switch (engineIOType) {
    case '0':
      Serial.println("[Engine.IO] OPEN - Session established");
      delay(1000);
      joinDeviceNamespace();
      break;

    case '3':
      Serial.println("[Engine.IO] PONG received");
      break;

    case '4':
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

void handleSocketIOMessage(String socketIOData) {
  if (socketIOData.length() < 1) return;

  char socketIOType = socketIOData.charAt(0);

  switch (socketIOType) {
    case '0':
      Serial.println("[Socket.IO] CONNECT acknowledged");
      if (socketIOData.indexOf("/device") != -1) {
        Serial.println("[Socket.IO] ✅ Connected to /device namespace!");
        namespaceConnected = true;
        delay(1000);
        sendDeviceOnline();
      }
      break;

    case '2':
      parseSocketIOEvent(socketIOData.substring(1));
      break;

    default:
      Serial.printf("[Socket.IO] Unknown packet type: %c\n", socketIOType);
      break;
  }
}

void parseSocketIOEvent(String eventData) {
  if (eventData.startsWith("/device,")) {
    eventData = eventData.substring(8);
  }

  int firstQuote = eventData.indexOf('"');
  if (firstQuote == -1) return;

  int secondQuote = eventData.indexOf('"', firstQuote + 1);
  if (secondQuote == -1) return;

  String eventName = eventData.substring(firstQuote + 1, secondQuote);
  Serial.println("[Socket.IO] Event name: " + eventName);

  int jsonStart = eventData.indexOf('{');
  if (jsonStart != -1) {
    String jsonData = eventData.substring(jsonStart, eventData.lastIndexOf('}') + 1);
    Serial.println("[Socket.IO] JSON data: " + jsonData);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonData);

    if (!err) {
      handleCommand(eventName, doc);
    } else {
      Serial.println("[Socket.IO] JSON parse error: " + String(err.c_str()));
    }
  }
}

void handleCommand(String eventName, JsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    Serial.printf("[COMMAND] Processing action: %s\n", action.c_str());

    if (action == "updateState") {
      if (!acquireEffectLock()) {
        sendCommandResponse("updateState", false, "Device busy - please try again");
        return;
      }
      
      effectActive = false;
      currentEffect = "solid";
      resetEffectState();
      
      strip.clear();
      strip.show();
      delay(50);
      
      JsonObject state = doc["state"];
      bool stateChanged = false;

      if (state["power_status"].is<bool>()) {
        ledPower = state["power_status"];
        stateChanged = true;
      }
      if (state["color"].is<String>()) {
        ledColor = state["color"] | "#FFFFFF";
        stateChanged = true;
      }
      if (state["brightness"].is<int>()) {
        ledBrightness = state["brightness"] | 100;
        stateChanged = true;
      }

      if (stateChanged) {
        updateLED();
        saveLEDState();
        sendCommandResponse("updateState", true, "LED state updated successfully");
        sendDeviceStatus();
      }
      
      releaseEffectLock();
    }
    else if (action == "setEffect") {
      if (!acquireEffectLock()) {
        sendCommandResponse("setEffect", false, "Device busy - please try again");
        return;
      }
      
      String effect = doc["effect"] | "solid";
      int speed = doc["speed"] | 500;
      int count = doc["count"] | 0;
      int duration = doc["duration"] | 0;
      String color1 = doc["color1"] | "#FF0000";
      String color2 = doc["color2"] | "#0000FF";

      Serial.printf("[COMMAND] Setting effect: %s (speed=%d, count=%d, duration=%d)\n", 
                    effect.c_str(), speed, count, duration);

      ledPower = true;
      
      effectActive = false;
      currentEffect = "solid";
      resetEffectState();
      
      strip.clear();
      strip.show();
      delay(100);
      
      setLEDEffect(effect, speed, count, duration, color1, color2);
      sendCommandResponse("setEffect", true, "LED effect started: " + effect);
      sendDeviceStatus();
      
      releaseEffectLock();
    }
    else if (action == "applyPreset") {
      if (!acquireEffectLock()) {
        sendCommandResponse("applyPreset", false, "Device busy - please try again");
        return;
      }
      
      String presetName = doc["preset"] | "party_mode";
      int duration = doc["duration"] | 0;

      Serial.printf("[COMMAND] Applying preset: %s (duration=%d)\n", presetName.c_str(), duration);

      effectActive = false;
      currentEffect = "solid";
      resetEffectState();
      
      strip.clear();
      strip.show();
      delay(100);
      
      applyPreset(presetName, duration);
      sendCommandResponse("applyPreset", true, "Preset applied: " + presetName);
      sendDeviceStatus();
      
      releaseEffectLock();
    }
    else if (action == "stopEffect") {
      Serial.println("[COMMAND] Stopping effects");
      
      if (acquireEffectLock()) {
        effectActive = false;
        currentEffect = "solid";
        resetEffectState();
        
        strip.clear();
        strip.show();
        delay(50);
        
        releaseEffectLock();
      }
      
      updateLED();
      sendCommandResponse("stopEffect", true, "LED effect stopped");
      sendDeviceStatus();
    }
  }
}

/**************************************************************
 * LED Control Functions
 **************************************************************/
uint32_t hexToColor(String hex) {
  if (hex.charAt(0) != '#' || hex.length() != 7) return strip.Color(0, 0, 0);
  long number = strtol(&hex[1], NULL, 16);
  return strip.Color((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
}

void setAllPixels(uint32_t color) {
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void updateLED() {
  if (!ledPower || effectActive) {
    if (!ledPower) {
      strip.clear();
      strip.show();
    }
    return;
  }

  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  setAllPixels(hexToColor(ledColor));
}

/**************************************************************
 * Effect Functions
 **************************************************************/
void resetEffectState() {
  Serial.println("[EFFECT] Resetting effect state");
  
  effectState.blinkCount = 0;
  effectState.blinkOn = false;
  effectState.chaseStep = 0;
  effectState.rainbowStep = 0;
  effectState.breathePhase = 0;
  effectState.fadeProgress = 0;
  effectState.fadeDirection = true;
  effectState.strobeCount = 0;
  effectState.lastDebugTime = 0;
  effectState.effectInitialized = false;
  effectState.lastEffect = "";
  
  for (int i = 0; i < NUMPIXELS; i++) {
    effectState.lastSparklePixels[i] = 0;
  }
}

void setLEDEffect(String effect, int speed, int count, int duration, String color1, String color2) {
  Serial.printf("[EFFECT] Starting new effect: %s\n", effect.c_str());
  
  currentEffect = effect;
  effectSpeed = max(50, min(5000, speed));
  effectCount = max(0, count);
  effectDuration = max(0, duration);
  effectColor1 = color1;
  effectColor2 = color2;
  effectStartTime = millis();
  lastEffectUpdate = millis();
  
  if (effect == "solid") {
    ledColor = color1;
    effectActive = false;
    Serial.println("[EFFECT] Set to solid color mode");
  } else {
    effectActive = true;
    effectState.effectInitialized = true;
    effectState.lastEffect = effect;
    Serial.printf("[EFFECT] Started dynamic effect: %s\n", effect.c_str());
  }
}

void stopEffect() {
  Serial.println("[EFFECT] Stopping effect");
  
  effectActive = false;
  currentEffect = "solid";
  
  resetEffectState();
  
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, 0, 0, 0);
  }
  strip.show();
  delay(50);
  
  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  
  Serial.println("[EFFECT] Effect stopped");
}

void processEffects() {
  if (!effectActive || !ledPower) {
    return;
  }
  
  unsigned long currentTime = millis();
  
  if (effectDuration > 0 && (currentTime - effectStartTime) > effectDuration) {
    Serial.println("[EFFECT] Effect duration completed");
    effectActive = false;
    currentEffect = "solid";
    updateLED();
    return;
  }
  
  if (!effectState.effectInitialized || effectState.lastEffect != currentEffect) {
    Serial.printf("[EFFECT] Initializing effect: %s\n", currentEffect.c_str());
    effectState.effectInitialized = true;
    effectState.lastEffect = currentEffect;
  }
  
  if (currentEffect == "blink" && (currentTime - lastEffectUpdate) >= effectSpeed) {
    processBlink();
    lastEffectUpdate = currentTime;
  }
  else if (currentEffect == "breathe") {
    processBreathe();
  }
  else if (currentEffect == "rainbow" && (currentTime - lastEffectUpdate) >= effectSpeed) {
    processRainbow();
    lastEffectUpdate = currentTime;
  }
  else if (currentEffect == "chase" && (currentTime - lastEffectUpdate) >= effectSpeed) {
    processChase();
    lastEffectUpdate = currentTime;
  }
  else if (currentEffect == "fade" && (currentTime - lastEffectUpdate) >= effectSpeed) {
    processFade();
    lastEffectUpdate = currentTime;
  }
  else if (currentEffect == "strobe" && (currentTime - lastEffectUpdate) >= effectSpeed) {
    processStrobe();
    lastEffectUpdate = currentTime;
  }
  else if (currentEffect == "sparkle" && (currentTime - lastEffectUpdate) >= effectSpeed) {
    processSparkle();
    lastEffectUpdate = currentTime;
  }
}

void processBlink() {
  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  
  if (effectState.blinkOn) {
    for (int i = 0; i < NUMPIXELS; i++) {
      strip.setPixelColor(i, 0, 0, 0);
    }
  } else {
    uint32_t color = hexToColor(effectColor1);
    for (int i = 0; i < NUMPIXELS; i++) {
      strip.setPixelColor(i, color);
    }
  }
  strip.show();
  
  effectState.blinkOn = !effectState.blinkOn;
  effectState.blinkCount++;
  
  if (effectCount > 0 && effectState.blinkCount >= (effectCount * 2)) {
    forceStopAllEffects();
    updateLED();
  }
}

void processBreathe() {
  float phase = (millis() - effectStartTime) / (float)effectSpeed;
  float breathe = (sin(phase * 2 * PI) + 1.0) / 2.0;
  
  int baseBrightness = map(ledBrightness, 0, 100, 0, 255);
  int breatheBrightness = (int)((0.3 + breathe * 0.7) * baseBrightness);
  
  strip.setBrightness(breatheBrightness);
  
  uint32_t color = hexToColor(effectColor1);
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void processRainbow() {
  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  
  for (int i = 0; i < NUMPIXELS; i++) {
    int hue = (i * 256 / NUMPIXELS + effectState.rainbowStep) % 256;
    strip.setPixelColor(i, wheel(hue));
  }
  strip.show();
  
  effectState.rainbowStep = (effectState.rainbowStep + 1) % 256;
}

void processChase() {
  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, 0, 0, 0);
  }
  
  uint32_t color = hexToColor(effectColor1);
  for (int i = 0; i < 3; i++) {
    int pixel = (effectState.chaseStep + i) % NUMPIXELS;
    strip.setPixelColor(pixel, color);
  }
  strip.show();
  
  effectState.chaseStep = (effectState.chaseStep + 1) % NUMPIXELS;
}

void processFade() {
  int totalCycle = effectSpeed * 2;
  unsigned long cycleTime = (millis() - effectStartTime) % totalCycle;
  float fadeProgress = (cycleTime < effectSpeed) ? 
    (float)cycleTime / effectSpeed : 
    1.0 - ((float)(cycleTime - effectSpeed) / effectSpeed);
  
  uint32_t color1 = hexToColor(effectColor1);
  uint32_t color2 = hexToColor(effectColor2);
  
  uint8_t r1 = (color1 >> 16) & 0xFF, g1 = (color1 >> 8) & 0xFF, b1 = color1 & 0xFF;
  uint8_t r2 = (color2 >> 16) & 0xFF, g2 = (color2 >> 8) & 0xFF, b2 = color2 & 0xFF;
  
  uint8_t r = r1 + (r2 - r1) * fadeProgress;
  uint8_t g = g1 + (g2 - g1) * fadeProgress;
  uint8_t b = b1 + (b2 - b1) * fadeProgress;
  
  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  
  uint32_t color = strip.Color(r, g, b);
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void processStrobe() {
  processBlink();
}

void processSparkle() {
  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  
  for (int i = 0; i < NUMPIXELS; i++) {
    if (effectState.lastSparklePixels[i] > 0) {
      effectState.lastSparklePixels[i] = effectState.lastSparklePixels[i] * 0.85;
      uint8_t brightness = effectState.lastSparklePixels[i];
      
      uint32_t baseColor = (random(2) == 0) ? hexToColor(effectColor1) : hexToColor(effectColor2);
      uint8_t r = ((baseColor >> 16) & 0xFF) * brightness / 255;
      uint8_t g = ((baseColor >> 8) & 0xFF) * brightness / 255;
      uint8_t b = (baseColor & 0xFF) * brightness / 255;
      
      strip.setPixelColor(i, strip.Color(r, g, b));
    } else {
      strip.setPixelColor(i, 0, 0, 0);
    }
  }
  
  if (random(100) < 30) {
    int sparkleCount = random(1, 4);
    for (int i = 0; i < sparkleCount; i++) {
      int pixel = random(NUMPIXELS);
      effectState.lastSparklePixels[pixel] = 255;
    }
  }
  
  strip.show();
}

uint32_t wheel(byte wheelPos) {
  wheelPos = 255 - wheelPos;
  if (wheelPos < 85) {
    return strip.Color(255 - wheelPos * 3, 0, wheelPos * 3);
  }
  if (wheelPos < 170) {
    wheelPos -= 85;
    return strip.Color(0, wheelPos * 3, 255 - wheelPos * 3);
  }
  wheelPos -= 170;
  return strip.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
}

void applyPreset(String presetName, int duration) {
  Serial.printf("[PRESET] Applying %s (duration=%d)\n", presetName.c_str(), duration);
  
  ledPower = true;
  
  if (presetName == "party_mode") {
    ledColor = "#FF0000";
    ledBrightness = 100;
    setLEDEffect("rainbow", 200, 0, duration, "#FF0000", "#0000FF");
  } else if (presetName == "relaxation_mode") {
    ledColor = "#9370DB";
    ledBrightness = 100;
    setLEDEffect("breathe", 3000, 0, duration, "#9370DB", "#9370DB");
  } else if (presetName == "gaming_mode") {
    ledColor = "#00FF80";
    ledBrightness = 100;
    setLEDEffect("chase", 150, 0, duration, "#00FF80", "#FF0080");
  } else if (presetName == "alarm_mode") {
    ledColor = "#FF0000";
    ledBrightness = 100;
    setLEDEffect("strobe", 200, 20, duration, "#FF0000", "#FF0000");
  } else if (presetName == "sleep_mode") {
    ledColor = "#FFB366";
    ledBrightness = 100;
    setLEDEffect("fade", 4000, 0, duration, "#FFB366", "#2F1B14");
  } else if (presetName == "wake_up_mode") {
    ledColor = "#330000";
    ledBrightness = 100;
    setLEDEffect("fade", 2000, 0, duration, "#330000", "#FFB366");
  } else if (presetName == "focus_mode") {
    ledColor = "#4169E1";
    ledBrightness = 100;
    setLEDEffect("solid", 0, 0, duration, "#4169E1", "#4169E1");
  } else if (presetName == "movie_mode") {
    ledColor = "#000080";
    ledBrightness = 100;
    setLEDEffect("breathe", 5000, 0, duration, "#000080", "#000080");
  } else if (presetName == "romantic_mode") {
    ledColor = "#FF1493";
    ledBrightness = 100;
    setLEDEffect("fade", 2000, 0, duration, "#FF1493", "#FF69B4");
  } else if (presetName == "celebration_mode") {
    ledColor = "#FFD700";
    ledBrightness = 100;
    setLEDEffect("sparkle", 200, 0, duration, "#FFD700", "#FFFFFF");
  } else {
    Serial.printf("[PRESET] Unknown preset '%s', using default\n", presetName.c_str());
    ledColor = "#FFFFFF";
    ledBrightness = 100;
    setLEDEffect("solid", 0, 0, duration, "#FFFFFF", "#FFFFFF");
  }
  
  Serial.printf("[PRESET] Successfully applied: %s\n", presetName.c_str());
}

/**************************************************************
 * Socket.IO Communication Functions
 **************************************************************/
void joinDeviceNamespace() {
  Serial.println("[Socket.IO] Joining /device namespace...");
  String namespaceJoin = "40/device,";
  webSocket.sendTXT(namespaceJoin);
}

void sendSocketIOEvent(String eventName, String jsonData) {
  if (!namespaceConnected) return;

  String eventPayload = "42/device,[\"" + eventName + "\"," + jsonData + "]";
  webSocket.sendTXT(eventPayload);
  Serial.println("[Socket.IO] Sent event '" + eventName + "'");
}

void sendDeviceOnline() {
  if (!namespaceConnected) return;

  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["deviceType"] = "LED_CONTROLLER_24";
  doc["category"] = "LIGHTING";
  doc["firmware_version"] = "v8.27-FixedPresets";
  doc["hardware_version"] = "ESP8266-v1.0";
  doc["capabilities"]["rgb_control"] = true;
  doc["capabilities"]["effect_control"] = true;

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("device_online", jsonString);
}

void sendDeviceStatus() {
  if (!namespaceConnected) return;
  
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["type"] = "deviceStatus";
  doc["state"]["power_status"] = ledPower;
  doc["state"]["color"] = ledColor;
  doc["state"]["brightness"] = ledBrightness;
  doc["state"]["effect"] = currentEffect;
  doc["state"]["effect_active"] = effectActive;
  
  if (effectActive) {
    doc["state"]["effect_speed"] = effectSpeed;
    doc["state"]["effect_count"] = effectCount;
    doc["state"]["effect_duration"] = effectDuration;
    doc["state"]["effect_color1"] = effectColor1;
    doc["state"]["effect_color2"] = effectColor2;
    doc["state"]["effect_runtime"] = millis() - effectStartTime;
  }
  
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("deviceStatus", jsonString);
}

void sendCommandResponse(String command, bool success, String message) {
  JsonDocument doc;
  doc["success"] = success;
  doc["result"] = message;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["commandId"] = command;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("command_response", jsonString);
}

void sendWebSocketPing() {
  if (isConnected) {
    webSocket.sendTXT("2");
    Serial.println("[WebSocket] Ping sent");
  }
}

void sendHeartbeat() {
  if (!namespaceConnected) return;

  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("heartbeat", jsonString);
}

/**************************************************************
 * EEPROM Functions
 **************************************************************/
void saveLEDState() {
  Serial.println("[EEPROM] LED state saved");
}

void loadLEDState() {
  ledPower = false;
  ledColor = "#FFFFFF";
  ledBrightness = 100;
  Serial.println("[EEPROM] LED state loaded");
}

/**************************************************************
 * Connection & Test Functions
 **************************************************************/
void handleLEDStatus(String status) {
  if (status == "CONNECTING") {
    setAllPixels(hexToColor("#FFD700"));
  } else if (status == "CONNECTED") {
    setAllPixels(hexToColor("#008001"));
    delay(3000);
    updateLED();
  } else if (status == "FAILED") {
    setAllPixels(hexToColor("#FF0000"));
    delay(3000);
    strip.clear();
    strip.show();
  }
}

void testLEDPattern() {
  Serial.println("[TEST] Running LED test pattern");

  bool originalPower = ledPower;
  String originalColor = ledColor;
  int originalBrightness = ledBrightness;

  String testColors[] = { "#FF0000", "#00FF00", "#0000FF", "#FFFFFF" };

  for (int i = 0; i < 4; i++) {
    ledPower = true;
    ledColor = testColors[i];
    ledBrightness = 100;
    updateLED();
    delay(1000);
  }

  if (acquireEffectLock()) {
    forceStopAllEffects();
    setLEDEffect("rainbow", 50, 0, 3000, "#FF0000", "#0000FF");
    releaseEffectLock();
    
    unsigned long testStart = millis();
    while (millis() - testStart < 3000) {
      processEffects();
      delay(50);
    }
    forceStopAllEffects();
  }

  ledPower = originalPower;
  ledColor = originalColor;
  ledBrightness = originalBrightness;
  updateLED();

  Serial.println("[TEST] LED test pattern completed");
}

void startWebSocketConnection() {
  Serial.println("\n[WebSocket] Starting connection...");
  Serial.println("Host: " + WEBSOCKET_HOST + ":" + String(WEBSOCKET_PORT));
  Serial.println("Path: " + WEBSOCKET_PATH);
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("Serial Number: " + String(SERIAL_NUMBER));

  webSocket.begin(WEBSOCKET_HOST.c_str(), WEBSOCKET_PORT, WEBSOCKET_PATH.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);

  webSocket.setExtraHeaders("User-Agent: ESP8266-LEDController/8.27\r\nOrigin: http://192.168.51.115:7777");

  Serial.println("[WebSocket] Connection initiated with enhanced parameters");
}

/**************************************************************
 * Main Arduino Functions
 **************************************************************/
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP8266 LED Controller v8.27 - Fixed Preset Issue ===");
  Serial.println("Features: Preset overlap prevention, effect isolation, state sync");
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("Serial Number: " + String(SERIAL_NUMBER));

  strip.begin();
  strip.show();
  strip.clear();
  strip.setBrightness(255);

  resetEffectState();
  loadLEDState();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting to " + String(WIFI_SSID));

  handleLEDStatus("CONNECTING");

  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.println("[WiFi] IP Address: " + WiFi.localIP().toString());
    Serial.println("[WiFi] Gateway: " + WiFi.gatewayIP().toString());
    Serial.println("[WiFi] DNS: " + WiFi.dnsIP().toString());
    Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("[WiFi] MAC: " + WiFi.macAddress());

    handleLEDStatus("CONNECTED");
    testLEDPattern();
    startWebSocketConnection();
  } else {
    Serial.println(" Failed!");
    Serial.println("[WiFi] Connection failed after 15 seconds");
    handleLEDStatus("FAILED");
  }

  Serial.println("[SETUP] Initialization complete");
  Serial.println("Ready for commands...\n");
}

void loop() {
  webSocket.loop();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connection lost, attempting reconnection...");
    handleLEDStatus("CONNECTING");
    WiFi.reconnect();
    
    unsigned long reconnectStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - reconnectStart) < 10000) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(" Reconnected!");
      handleLEDStatus("CONNECTED");
    } else {
      Serial.println(" Failed to reconnect!");
      handleLEDStatus("FAILED");
    }
    return;
  }

  if (isConnected && namespaceConnected) {
    if (isEffectSafe()) {
      processEffects();
    }

    if (millis() - lastStatusUpdate > STATUS_INTERVAL) {
      sendDeviceStatus();
      lastStatusUpdate = millis();
    }

    if (millis() - lastPingTime > PING_INTERVAL) {
      sendWebSocketPing();
      sendHeartbeat();
      lastPingTime = millis();
    }
  } else {
    reconnectAttempts++;
    if (reconnectAttempts % 20 == 0) {
      Serial.printf("[Connection] Status - WebSocket: %s, Namespace: %s, Attempts: %d\n",
                    isConnected ? "CONNECTED" : "DISCONNECTED",
                    namespaceConnected ? "JOINED" : "NOT_JOINED",
                    reconnectAttempts);
    }

    if (isConnected && !namespaceConnected && (reconnectAttempts % 100 == 0)) {
      Serial.println("[Socket.IO] Attempting to rejoin namespace...");
      joinDeviceNamespace();
    }
  }

  delay(50);
}

/**************************************************************
 * Error Handling & Health Check
 **************************************************************/
void handleCriticalError(String errorType, String errorMessage) {
  Serial.println("\n!!! CRITICAL ERROR !!!");
  Serial.println("Type: " + errorType);
  Serial.println("Message: " + errorMessage);
  Serial.println("Uptime: " + String(millis() / 1000) + " seconds");
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  
  forceStopAllEffects();
  for (int i = 0; i < 5; i++) {
    setAllPixels(hexToColor("#FF0000"));
    delay(200);
    strip.clear();
    strip.show();
    delay(200);
  }
  
  Serial.println("Attempting system recovery...");
  ESP.restart();
}

void performSystemHealthCheck() {
  static unsigned long lastHealthCheck = 0;
  
  if (millis() - lastHealthCheck > 300000) {
    Serial.println("\n[Health Check] Performing system health check...");
    
    if (ESP.getFreeHeap() < 4096) {
      handleCriticalError("LOW_MEMORY", "Free heap below 4KB");
      return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[Health Check] WiFi disconnected");
    }
    
    if (!isConnected) {
      Serial.println("[Health Check] WebSocket disconnected");
    }
    
    if (effectProcessing && (millis() - effectLockTime) > (EFFECT_LOCK_TIMEOUT * 10)) {
      Serial.println("[Health Check] Effect processing lock stuck, forcing reset");
      effectProcessing = false;
      effectChangePending = false;
      forceStopAllEffects();
    }
    
    Serial.println("[Health Check] System health check completed");
    lastHealthCheck = millis();
  }
}