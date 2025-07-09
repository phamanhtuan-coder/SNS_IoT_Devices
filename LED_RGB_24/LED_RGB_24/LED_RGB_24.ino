/**************************************************************
 * ESP8266 RGB LED Controller - UDP WiFi + SSL Railway Connection
 * 
 * Version: v9.0 - UDP Config + SSL Support
 **************************************************************/

#define SERIAL_NUMBER "SERL12JUN2501LED24RGB001"
#define DEVICE_ID "DEVIC5755"

#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

// Hotspot configuration
const char* HOTSPOT_SSID = "ESP8266-LED-Config";
const char* HOTSPOT_PASSWORD = "ledconfig123";

// Server configuration (Railway SSL)
const char* WEBSOCKET_HOST = "iothomeconnectapiv2-production.up.railway.app";
const uint16_t WEBSOCKET_PORT = 443;
const char* WEBSOCKET_PATH_TEMPLATE = "/socket.io/?EIO=3&transport=websocket&serialNumber=%s&isIoTDevice=true";

// Hardware
WebSocketsClient webSocket;
WiFiUDP udp;
ESP8266WebServer httpServer(80);

#define LED_PIN D6
#define NUMPIXELS 24
#define UDP_PORT 8888

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// EEPROM configuration
#define EEPROM_SIZE 512
#define WIFI_SSID_ADDR 0
#define WIFI_PASS_ADDR 100
#define LED_STATE_ADDR 200

// Network credentials
String wifiSSID = "";
String wifiPassword = "";

// State variables
bool isConnected = false;
bool namespaceConnected = false;
bool isHotspotMode = false;
int reconnectAttempts = 0;

// Timing
unsigned long lastStatusUpdate = 0;
unsigned long lastPingTime = 0;
const unsigned long STATUS_INTERVAL = 5000;
const unsigned long PING_INTERVAL = 25000;

// LED State
bool ledPower = false;
String ledColor = "#FFFFFF";
int ledBrightness = 100;

// Effects
String currentEffect = "solid";
bool effectActive = false;
int effectSpeed = 500;
int effectCount = 0;
int effectDuration = 0;
unsigned long effectStartTime = 0;
unsigned long lastEffectUpdate = 0;
String effectColor1 = "#FF0000";
String effectColor2 = "#0000FF";

// Effect state
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
  int colorWaveOffset = 0;
  int discoColorIndex = 0;
  int rainbowMoveStep = 0;
  int meteorPos = 0;
  int meteorTail = 5;
  float pulsePhase = 0;
  int wavePosition = 0;
  bool meteorDirection = true;
  unsigned long lastDiscoChange = 0;
  int twinklePixels[24] = {0};
  int fireworksCenter = 0;
  bool fireworksExpanding = true;
  int fireworksRadius = 0;
} effectState;

// Buffers
static char websocketPath[256];

// HTML Configuration Page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<title>ESP8266 LED Setup</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}
.container{max-width:500px;margin:0 auto;background:#fff;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}
h1{color:#333;text-align:center;margin-bottom:30px}
.form-group{margin-bottom:20px}
label{display:block;margin-bottom:5px;font-weight:bold;color:#555}
input[type="text"],input[type="password"],input[type="color"],input[type="range"]{width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;font-size:16px;box-sizing:border-box}
input[type="submit"]{width:100%;padding:12px;background:#007bff;color:#fff;border:none;border-radius:5px;font-size:16px;cursor:pointer;transition:background 0.3s}
input[type="submit"]:hover{background:#0056b3}
.info{background:#e9ecef;padding:15px;border-radius:5px;margin-bottom:20px}
.status{text-align:center;margin-top:20px;padding:10px;border-radius:5px;font-weight:bold}
.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
.color-preview{width:50px;height:50px;border-radius:5px;border:2px solid #ddd;display:inline-block;margin-left:10px}
</style>
</head><body>
<div class="container">
<h1>💡 ESP8266 LED Setup</h1>
<div class="info">
<strong>Device:</strong> %s<br>
<strong>Version:</strong> v9.0<br>
<strong>Features:</strong> RGB Control, Effects, Presets
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
<label for='defaultColor'>Default LED Color:</label>
<input type='color' id='defaultColor' name='defaultColor' value='#FFFFFF'>
<div class="color-preview" style="background-color:#FFFFFF"></div>
</div>
<div class='form-group'>
<label for='defaultBrightness'>Default Brightness (0-100):</label>
<input type='range' id='defaultBrightness' name='defaultBrightness' min='0' max='100' value='100'>
<span id='brightnessValue'>100%</span>
</div>
<input type='submit' value='💾 Save Configuration'>
</form>
<div class="status">
<p>📡 Device will restart after saving configuration</p>
</div>
</div>
<script>
document.getElementById('defaultColor').addEventListener('change', function() {
  document.querySelector('.color-preview').style.backgroundColor = this.value;
});
document.getElementById('defaultBrightness').addEventListener('input', function() {
  document.getElementById('brightnessValue').textContent = this.value + '%';
});
</script>
</body></html>
)rawliteral";

/**************************************************************
 * EEPROM FUNCTIONS
 **************************************************************/
void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  Serial.println("[EEPROM] Initialized");
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
  Serial.printf("[EEPROM] Wrote string at %d: %s\n", address, data.c_str());
}

void saveLEDState() {
  StaticJsonDocument<200> config;
  config["power"] = ledPower;
  config["color"] = ledColor;
  config["brightness"] = ledBrightness;
  
  String configStr;
  serializeJson(config, configStr);
  writeEEPROMString(LED_STATE_ADDR, configStr, 200);
}

void loadLEDState() {
  String configStr = readEEPROMString(LED_STATE_ADDR, 200);
  if (configStr.length() > 0) {
    StaticJsonDocument<200> config;
    if (deserializeJson(config, configStr) == DeserializationError::Ok) {
      ledPower = config["power"] | false;
      ledColor = config["color"] | "#FFFFFF";
      ledBrightness = config["brightness"] | 100;
      Serial.println("[Config] LED state loaded from EEPROM");
    }
  }
}

/**************************************************************
 * WIFI FUNCTIONS
 **************************************************************/
bool connectToWiFi(const String& ssid, const String& password) {
  Serial.printf("[WiFi] Connecting to %s...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Signal: %d dBm\n", WiFi.RSSI());
    return true;
  }
  
  Serial.println("[WiFi] Connection failed");
  return false;
}

void startHotspot() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  Serial.printf("[Hotspot] Started: %s\n", HOTSPOT_SSID);
  Serial.printf("[Hotspot] IP: %s\n", WiFi.softAPIP().toString().c_str());
  isHotspotMode = true;
}

/**************************************************************
 * UDP FUNCTIONS
 **************************************************************/
void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[512];
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = 0;
      Serial.printf("[UDP] Received: %s\n", packetBuffer);
      
      StaticJsonDocument<300> doc;
      DeserializationError error = deserializeJson(doc, packetBuffer);
      
      if (!error && doc.containsKey("ssid")) {
        String ssid = doc["ssid"].as<String>();
        String password = doc["password"].as<String>();
        String defaultColor = doc["defaultColor"] | "#FFFFFF";
        int defaultBrightness = doc["defaultBrightness"] | 100;
        
        Serial.println("[UDP] Received WiFi credentials and config");
        
        // Save WiFi credentials
        writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
        writeEEPROMString(WIFI_PASS_ADDR, password, 100);
        
        // Save LED configuration
        ledColor = defaultColor;
        ledBrightness = defaultBrightness;
        saveLEDState();
        
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

/**************************************************************
 * HTTP SERVER FUNCTIONS
 **************************************************************/
void setupHttpServer() {
  // Configuration page
  httpServer.on("/", HTTP_GET, []() {
    char html[6000];
    snprintf(html, sizeof(html), CONFIG_HTML, SERIAL_NUMBER);
    httpServer.send(200, "text/html", html);
  });
  
  // Save configuration
  httpServer.on("/save_config", HTTP_POST, []() {
    String ssid = httpServer.arg("ssid");
    String password = httpServer.arg("password");
    String defaultColor = httpServer.arg("defaultColor");
    int defaultBrightness = httpServer.arg("defaultBrightness").toInt();
    
    writeEEPROMString(WIFI_SSID_ADDR, ssid, 100);
    writeEEPROMString(WIFI_PASS_ADDR, password, 100);
    
    ledColor = defaultColor;
    ledBrightness = defaultBrightness;
    saveLEDState();
    
    httpServer.send(200, "text/html", 
      "<html><body style='font-family:Arial;text-align:center;padding:50px'>"
      "<h1>✅ Configuration Saved</h1>"
      "<p>Device will restart and connect to: <strong>" + ssid + "</strong></p>"
      "<p>Default color: <span style='color:" + defaultColor + "'>■</span> " + defaultColor + "</p>"
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
    doc["status"] = ledPower ? "on" : "off";
    doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"] = WiFi.localIP().toString();
    doc["wifi"]["rssi"] = WiFi.RSSI();
    doc["led"]["power"] = ledPower;
    doc["led"]["color"] = ledColor;
    doc["led"]["brightness"] = ledBrightness;
    doc["led"]["effect"] = currentEffect;
    doc["uptime"] = millis() / 1000;
    
    String response;
    serializeJson(doc, response);
    
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", response);
  });
  
  httpServer.begin();
  Serial.println("[HTTP] Server started on port 80");
}

/**************************************************************
 * LED CONTROL FUNCTIONS
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

void resetEffectState() {
  effectState.blinkCount = 0;
  effectState.blinkOn = false;
  effectState.chaseStep = 0;
  effectState.rainbowStep = 0;
  effectState.breathePhase = 0;
  effectState.fadeProgress = 0;
  effectState.fadeDirection = true;
  effectState.strobeCount = 0;
  effectState.colorWaveOffset = 0;
  effectState.discoColorIndex = 0;
  effectState.rainbowMoveStep = 0;
  effectState.meteorPos = 0;
  effectState.meteorTail = 5;
  effectState.pulsePhase = 0;
  effectState.wavePosition = 0;
  effectState.meteorDirection = true;
  effectState.lastDiscoChange = 0;
  effectState.fireworksCenter = 0;
  effectState.fireworksExpanding = true;
  effectState.fireworksRadius = 0;
  
  for (int i = 0; i < NUMPIXELS; i++) {
    effectState.lastSparklePixels[i] = 0;
    effectState.twinklePixels[i] = 0;
  }
}

void setLEDEffect(String effect, int speed, int count, int duration, String color1, String color2) {
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
  } else {
    effectActive = true;
    resetEffectState();
  }
}

void processEffects() {
  if (!effectActive || !ledPower) return;
  
  unsigned long currentTime = millis();
  
  if (effectDuration > 0 && (currentTime - effectStartTime) > effectDuration) {
    effectActive = false;
    currentEffect = "solid";
    updateLED();
    return;
  }
  
  // Process different effects
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
    strip.clear();
  } else {
    setAllPixels(hexToColor(effectColor1));
  }
  strip.show();
  
  effectState.blinkOn = !effectState.blinkOn;
  effectState.blinkCount++;
  
  if (effectCount > 0 && effectState.blinkCount >= (effectCount * 2)) {
    effectActive = false;
    updateLED();
  }
}

void processBreathe() {
  float phase = (millis() - effectStartTime) / (float)effectSpeed;
  float breathe = (sin(phase * 2 * PI) + 1.0) / 2.0;
  
  int baseBrightness = map(ledBrightness, 0, 100, 0, 255);
  int breatheBrightness = (int)((0.3 + breathe * 0.7) * baseBrightness);
  
  strip.setBrightness(breatheBrightness);
  setAllPixels(hexToColor(effectColor1));
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
  
  strip.clear();
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
  setAllPixels(strip.Color(r, g, b));
}

void processStrobe() {
  int brightnessScaled = map(ledBrightness, 0, 100, 0, 255);
  strip.setBrightness(brightnessScaled);
  
  unsigned long strobeSpeed = max(30, effectSpeed / 8);
  bool strobeOn = ((millis() / strobeSpeed) % 2) == 0;
  
  if (strobeOn) {
    setAllPixels(hexToColor(effectColor1));
  } else {
    strip.clear();
    strip.show();
  }
  
  if (effectCount > 0) {
    static unsigned long lastStrobeTime = 0;
    if (millis() - lastStrobeTime >= strobeSpeed * 2) {
      effectState.strobeCount++;
      lastStrobeTime = millis();
      
      if (effectState.strobeCount >= effectCount) {
        effectActive = false;
        updateLED();
      }
    }
  }
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
  ledPower = true;
  
  if (presetName == "party_mode") {
    setLEDEffect("rainbow", 80, 0, duration, "#FF00FF", "#00FFFF");
  } 
  else if (presetName == "relaxation_mode") {
    setLEDEffect("breathe", 5000, 0, duration, "#6A5ACD", "#9370DB");
  } 
  else if (presetName == "alarm_mode") {
    setLEDEffect("strobe", 100, 30, duration, "#FF0000", "#FFFFFF");
  } 
  else {
    ledColor = "#FFFFFF";
    setLEDEffect("solid", 0, 0, duration, "#FFFFFF", "#FFFFFF");
  }
}

/**************************************************************
 * WEBSOCKET FUNCTIONS (SSL)
 **************************************************************/
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] Disconnected from server");
      isConnected = false;
      namespaceConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.printf("[WebSocket] Connected to server: %s\n", payload);
      Serial.println("[WebSocket] ✅ SSL WebSocket connection established");
      isConnected = true;
      namespaceConnected = false;
      reconnectAttempts = 0;
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        handleWebSocketMessage(message);
        break;
      }
      
    case WStype_ERROR:
      Serial.printf("[WebSocket] Error: %s\n", payload);
      break;
  }
}

void handleWebSocketMessage(String message) {
  if (message.length() < 1) return;
  
  char engineIOType = message.charAt(0);
  
  switch(engineIOType) {
    case '0': // Engine.IO OPEN
      Serial.println("[Engine.IO] OPEN - Session established");
      delay(1000);
      joinDeviceNamespace();
      break;
      
    case '2': // Engine.IO PING
      Serial.println("[Engine.IO] PING received");
      webSocket.sendTXT("3"); // Send PONG
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
  }
}

void handleSocketIOMessage(String socketIOData) {
  if (socketIOData.length() < 1) return;
  
  char socketIOType = socketIOData.charAt(0);
  
  switch(socketIOType) {
    case '0': // Socket.IO CONNECT
      Serial.println("[Socket.IO] CONNECT acknowledged");
      if (socketIOData.indexOf("/device") != -1) {
        Serial.println("[Socket.IO] ✅ Connected to /device namespace!");
        namespaceConnected = true;
        delay(1000);
        sendDeviceOnline();
      }
      break;
      
    case '2': // Socket.IO EVENT
      parseSocketIOEvent(socketIOData.substring(1));
      break;
      
    case '4': // Socket.IO ERROR
      Serial.println("[Socket.IO] ERROR received");
      if (!namespaceConnected) {
        delay(2000);
        joinDeviceNamespace();
      }
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
  
  int jsonStart = eventData.indexOf('{');
  if (jsonStart != -1) {
    String jsonData = eventData.substring(jsonStart, eventData.lastIndexOf('}') + 1);
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonData);
    
    if (!err) {
      handleCommand(eventName, doc);
    }
  }
}

void handleCommand(String eventName, JsonDocument& doc) {
  if (eventName == "command") {
    String action = doc["action"];
    
    if (action == "updateState") {
      effectActive = false;
      currentEffect = "solid";
      resetEffectState();
      
      JsonObject state = doc["state"];
      if (state["power_status"].is<bool>()) {
        ledPower = state["power_status"];
      }
      if (state["color"].is<String>()) {
        ledColor = state["color"] | "#FFFFFF";
      }
      if (state["brightness"].is<int>()) {
        ledBrightness = state["brightness"] | 100;
      }
      
      updateLED();
      saveLEDState();
      sendCommandResponse("updateState", true, "LED state updated successfully");
      sendDeviceStatus();
    }
    else if (action == "setEffect") {
      String effect = doc["effect"] | "solid";
      int speed = doc["speed"] | 500;
      int count = doc["count"] | 0;
      int duration = doc["duration"] | 0;
      String color1 = doc["color1"] | "#FF0000";
      String color2 = doc["color2"] | "#0000FF";
      
      ledPower = true;
      setLEDEffect(effect, speed, count, duration, color1, color2);
      sendCommandResponse("setEffect", true, "LED effect started: " + effect);
      sendDeviceStatus();
    }
    else if (action == "applyPreset") {
      String presetName = doc["preset"] | "party_mode";
      int duration = doc["duration"] | 0;
      
      applyPreset(presetName, duration);
      sendCommandResponse("applyPreset", true, "Preset applied: " + presetName);
      sendDeviceStatus();
    }
    else if (action == "stopEffect") {
      effectActive = false;
      currentEffect = "solid";
      resetEffectState();
      updateLED();
      sendCommandResponse("stopEffect", true, "LED effect stopped");
      sendDeviceStatus();
    }
  }
}

void joinDeviceNamespace() {
  Serial.println("[Socket.IO] Joining /device namespace...");
  webSocket.sendTXT("40/device,");
}

void sendSocketIOEvent(String eventName, String jsonData) {
  if (!namespaceConnected) return;
  
  String eventPayload = "42/device,[\"" + eventName + "\"," + jsonData + "]";
  webSocket.sendTXT(eventPayload);
}

void sendDeviceOnline() {
  if (!namespaceConnected) return;
  
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["serialNumber"] = SERIAL_NUMBER;
  doc["deviceType"] = "LED_CONTROLLER_24";
  doc["category"] = "LIGHTING";
  doc["firmware_version"] = "v9.0-UDP-SSL";
  doc["hardware_version"] = "ESP8266-v1.0";
  doc["capabilities"]["rgb_control"] = true;
  doc["capabilities"]["effect_control"] = true;
  doc["capabilities"]["preset_control"] = true;
  
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
  doc["commandId"] = command;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  sendSocketIOEvent("command_response", jsonString);
}

void handleLEDStatus(String status) {
  if (status == "CONNECTING") {
    setAllPixels(hexToColor("#FFD700")); // Gold
  } else if (status == "CONNECTED") {
    setAllPixels(hexToColor("#008001")); // Green
    delay(3000);
    updateLED();
  } else if (status == "FAILED") {
    setAllPixels(hexToColor("#FF0000")); // Red
    delay(3000);
    strip.clear();
    strip.show();
  }
}

void setupWebSocketConnection() {
  snprintf(websocketPath, sizeof(websocketPath), WEBSOCKET_PATH_TEMPLATE, SERIAL_NUMBER);
  
  Serial.println("\n[WebSocket] Starting SSL connection...");
  Serial.printf("Host: %s:%d\n", WEBSOCKET_HOST, WEBSOCKET_PORT);
  Serial.printf("Path: %s\n", websocketPath);
  
  // SSL WebSocket connection
  webSocket.beginSSL(WEBSOCKET_HOST, WEBSOCKET_PORT, websocketPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  Serial.println("[WebSocket] SSL connection initiated");
}

/**************************************************************
 * MAIN FUNCTIONS
 **************************************************************/
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP8266 LED Controller v9.0 - UDP + SSL ===");
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("Serial Number: " + String(SERIAL_NUMBER));
  
  // Initialize LED strip
  strip.begin();
  strip.show();
  strip.clear();
  strip.setBrightness(255);
  
  // Initialize EEPROM and load configuration
  initEEPROM();
  loadLEDState();
  resetEffectState();
  
  // Load WiFi credentials
  wifiSSID = readEEPROMString(WIFI_SSID_ADDR, 100);
  wifiPassword = readEEPROMString(WIFI_PASS_ADDR, 100);
  
  // Connect to WiFi or start hotspot
  if (wifiSSID.length() > 0 && connectToWiFi(wifiSSID, wifiPassword)) {
    handleLEDStatus("CONNECTED");
    setupWebSocketConnection();
  } else {
    startHotspot();
    udp.begin(UDP_PORT);
    Serial.printf("[UDP] Listening on port %d\n", UDP_PORT);
    handleLEDStatus("CONNECTING");
  }
  
  // Start HTTP server
  setupHttpServer();
  
  Serial.println("[SETUP] Initialization complete");
}

void loop() {
  if (!isHotspotMode) {
    webSocket.loop();
  }
  
  httpServer.handleClient();
  
  if (isHotspotMode) {
    handleUDP();
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Connection lost, reconnecting...");
      handleLEDStatus("CONNECTING");
      if (!connectToWiFi(wifiSSID, wifiPassword)) {
        startHotspot();
        udp.begin(UDP_PORT);
      }
      return;
    }
    
    if (isConnected && namespaceConnected) {
      processEffects();
      
      if (millis() - lastStatusUpdate > STATUS_INTERVAL) {
        sendDeviceStatus();
        lastStatusUpdate = millis();
      }
      
      if (millis() - lastPingTime > PING_INTERVAL) {
        webSocket.sendTXT("2"); // Send ping
        lastPingTime = millis();
      }
    }
  }
  
  delay(50);
}