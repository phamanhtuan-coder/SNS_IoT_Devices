// ========================================
// ARDUINO MEGA GARDEN HUB - INTEGRATED SENSORS & DISPLAY
// ========================================
#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define FIRMWARE_VERSION "3.0.0"
#define HUB_ID "MEGA_HUB_GARDEN_INTEGRATED_001"

// ===== ARDUINO MEGA CENTRAL HUB =====
// Serial0 (0,1): Debug Output / USB
// Serial1 (18,19): ESP8266 Socket Hub
// Serial2 (16,17): ESP8266 Door Hub (PCA9685)
// Serial3: REMOVED - No longer needed

// ===== GARDEN SENSOR PINS =====
#define DHT_PIN 22
#define DHT_TYPE DHT11
#define WATER_SENSOR_PIN A0
#define LIGHT_SENSOR_PIN 24
#define SOIL_MOISTURE_PIN A2
#define PUMP_RELAY_PIN 23

// ===== RGB LED PINS (Garden Status) =====
#define RGB_RED_PIN 5
#define RGB_GREEN_PIN 6
#define RGB_BLUE_PIN 7

// ===== I2C DISPLAY PINS (Arduino Mega) =====
// SCL: Pin 21 (Clock)
// SDA: Pin 20 (Data)
// Both OLED and LCD share the same I2C bus

// ===== I2C DISPLAY ADDRESSES =====
#define LCD_I2C_ADDRESS 0x27    // LCD 16x2 I2C address (could be 0x3F)
#define OLED_I2C_ADDRESS 0x3C   // OLED 0.96" I2C address (could be 0x3D)
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// ===== 8-CHANNEL RELAY PINS (Active HIGH) =====
#define RELAY_1_PIN 30  // Fan control
#define RELAY_2_PIN 31  // Alarm control
#define RELAY_3_PIN 32  // Light 1
#define RELAY_4_PIN 33  // Light 2
#define RELAY_5_PIN 34  // Pump 2 (backup)
#define RELAY_6_PIN 35  // Heater
#define RELAY_7_PIN 36  // Cooler/Fan 2
#define RELAY_8_PIN 37  // Reserve

// ===== RELAY STATUS LED PINS (Individual LED for each relay) =====
#define RELAY_LED_1 44  // Fan LED
#define RELAY_LED_2 45  // Alarm LED
#define RELAY_LED_3 46  // Light 1 LED
#define RELAY_LED_4 47  // Light 2 LED
#define RELAY_LED_5 48  // Pump 2 LED
#define RELAY_LED_6 49  // Heater LED
#define RELAY_LED_7 50  // Cooler LED
#define RELAY_LED_8 51  // Reserve LED

// ===== FUTURE TOUCH SENSOR/SWITCH PINS (Local Relay Control) =====
// Freed up pins 38-43 from LCD, now available for more sensors/expansion
#define TOUCH_1_PIN 25  // Fan touch switch
#define TOUCH_2_PIN 26  // Alarm reset switch
#define TOUCH_3_PIN 27  // Light 1 switch
#define TOUCH_4_PIN 28  // Light 2 switch
#define TOUCH_5_PIN 29  // Pump 2 switch
#define TOUCH_6_PIN A3  // Heater switch
#define TOUCH_7_PIN A4  // Cooler switch
#define TOUCH_8_PIN A5  // Reserve switch

// ===== ADDITIONAL EXPANSION PINS (Available) =====
// Pins 38-43: Now free for more sensors, actuators, or I/O
// Could be used for: more touch sensors, encoders, additional relays, etc.

// ===== RGB STATUS COLORS =====
enum GardenStatus {
  STATUS_GOOD = 0,
  STATUS_WATERING = 1,
  STATUS_DRY = 2,
  STATUS_ERROR = 3,
  STATUS_RAIN = 4,
  STATUS_NIGHT = 5,
  STATUS_OFFLINE = 6
};

// ===== SENSOR OBJECTS =====
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ===== RELAY DEVICE DATA =====
struct RelayDevice {
  String serialNumber;
  String deviceName;
  int relayPin;
  int statusLedPin;
  int touchPin;
  bool isOn;
  bool canToggle;
  bool fireOverride;
  bool localControl;  // Enable local touch control
  unsigned long lastToggle;
  unsigned long lastTouchTime;
  String status;
};

RelayDevice relayDevices[8] = {
  { "RELAY27JUN2501FAN001CONTROL001", "Fan", RELAY_1_PIN, RELAY_LED_1, TOUCH_1_PIN, false, true, false, true, 0, 0, "off" },
  { "RELAY27JUN2501ALARM01CONTROL01", "Alarm", RELAY_2_PIN, RELAY_LED_2, TOUCH_2_PIN, false, true, true, true, 0, 0, "off" },
  { "RELAY27JUN2501LIGHT001CONTROL1", "Light1", RELAY_3_PIN, RELAY_LED_3, TOUCH_3_PIN, false, true, false, true, 0, 0, "off" },
  { "RELAY27JUN2501LIGHT002CONTROL1", "Light2", RELAY_4_PIN, RELAY_LED_4, TOUCH_4_PIN, false, true, false, true, 0, 0, "off" },
  { "RELAY27JUN2501PUMP002CONTROL01", "Pump2", RELAY_5_PIN, RELAY_LED_5, TOUCH_5_PIN, false, true, false, false, 0, 0, "off" },
  { "RELAY27JUN2501HEATER1CONTROL01", "Heater", RELAY_6_PIN, RELAY_LED_6, TOUCH_6_PIN, false, true, false, true, 0, 0, "off" },
  { "RELAY27JUN2501COOLER1CONTROL01", "Cooler", RELAY_7_PIN, RELAY_LED_7, TOUCH_7_PIN, false, true, false, true, 0, 0, "off" },
  { "RELAY27JUN2501RESERVE8CONTROL1", "Reserve", RELAY_8_PIN, RELAY_LED_8, TOUCH_8_PIN, false, true, false, false, 0, 0, "off" }
};

const int TOTAL_RELAY_DEVICES = 8;

// ===== GARDEN SENSOR DATA =====
struct GardenData {
  float temperature;
  float humidity;
  int soilMoisture;
  int lightLevel;
  bool rainDetected;
  bool pumpRunning;
  String currentTime;
  String rgbStatus;
  bool fireDetected;
  unsigned long lastUpdated;
  
  // Display tracking
  int lcdPage;
  int oledPage;
  unsigned long lastLcdUpdate;
  unsigned long lastOledUpdate;
  unsigned long lastPageChange;
};

GardenData gardenData;

// ===== DOOR DEVICE STATE MANAGEMENT =====
struct DeviceState {
  String serialNumber;
  String lastAction;
  bool doorOpen;
  int servoAngle;
  int pwmChannel;
  bool isOnline;
  unsigned long lastSeen;
  String status;
  String doorType;
};

// Door database for ESP8266 Door Hub with PCA9685
DeviceState devices[9] = {
  {"SERL27JUN2501JYR2RKVVX08V40YMGTW", "", false, 0, 0, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVR0SC7SJ8P8DD", "", false, 0, 1, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVRNHS46VR6AS1", "", false, 0, 2, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVSE2RW7KQ4KMP", "", false, 0, 3, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTBZ40JPF88WP", "", false, 0, 4, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVTXNCK1GB3HBZ", "", false, 0, 5, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVS2P6XBVF1P2E", "", false, 0, 6, false, 0, "offline", "DUAL_PCA9685"},
  {"SERL27JUN2501JYR2RKVTH6PWR9ETXC2", "", false, 0, 7, false, 0, "offline", "SERVO_PCA9685"},
  {"SERL27JUN2501JYR2RKVVSBGRTM0TRFW", "", false, 0, 8, false, 0, "offline", "SERVO_PCA9685"}
};

const int TOTAL_DEVICES = 9;

// ===== SYSTEM STATUS =====
bool socketHubConnected = false;
bool doorHubConnected = false;
unsigned long lastSocketMessage = 0;
unsigned long lastDoorMessage = 0;

// ===== AUTOMATION SETTINGS =====
int soilMoistureThreshold = 30;
int lightThreshold = 200;
bool autoWateringEnabled = true;
bool autoLightingEnabled = true;
bool autoFanEnabled = true;
unsigned long pumpStartTime = 0;
const unsigned long maxPumpRunTime = 300000;

// ===== RGB STATUS =====
GardenStatus currentGardenStatus = STATUS_OFFLINE;
unsigned long lastRGBUpdate = 0;
bool rgbBlinkState = false;

// ===== DISPLAY SETTINGS =====
const int LCD_PAGES = 4;
const int OLED_PAGES = 3;
unsigned long lcdPageDuration = 3000; // 3 seconds per page
unsigned long oledPageDuration = 2000; // 2 seconds per page

// ===== STATISTICS =====
unsigned long commandsProcessed = 0;
unsigned long responsesForwarded = 0;
unsigned long sensorReadings = 0;
unsigned long relayCommands = 0;
unsigned long localTouchCommands = 0;
unsigned long systemUptime = 0;

// ===== FUNCTION PROTOTYPES =====
void initializeSensors();
void initializeDevices();
void initializeRGB();
void initializeRelays();
void initializeLCD();
void initializeTouchSensors();

void readGardenSensors();
void processGardenAutomation();
void processLightAutomation();
void processFanAutomation();
void startPump(String reason);
void stopPump(String reason);
void checkPumpSafety();

void updateRGBStatus();
GardenStatus determineGardenStatus();
void setRGBColor(int red, int green, int blue);
void handleRGBTest();

void updateLCDDisplay();
void displayLCDPage1(); // Temperature, Humidity, Time
void displayLCDPage2(); // Soil, Light, Pump status
void displayLCDPage3(); // Rain, Fire, Auto settings
void displayLCDPage4(); // System status, connections

void updateRelayStatusLEDs();
void handleLocalTouchSensors();
void checkTouchSensor(int relayIndex);

void toggleRelay(int deviceIndex, String reason);
void setRelayState(int deviceIndex, bool state, String reason);
int findRelayDeviceBySerial(String serialNumber);
void handleRelayCommand(String serialNumber, String action);
void checkFireAlarm();
void sendRelayStatusToSocket();
void sendRelayDeviceStatus(int deviceIndex);

void handleSocketHubMessage(String message);
void handleGardenSocketCommand(String cmdMessage);
void handleDoorSocketCommand(String cmdMessage);
void handleRelaySocketCommand(String cmdMessage);
void handleDoorHubMessage(String message);
void handleDoorHubResponse(String respMessage);

void forwardCommand(String serialNumber, String action);
int findDeviceBySerial(String serialNumber);
String extractJsonField(String json, String fieldName);
void sendErrorResponse(String serialNumber, String action, String error);
void sendRelayResponse(String serialNumber, String action, bool success, String result);
void checkSystemHealth();
void sendSystemHeartbeat();
void printSystemStatus();
int freeMemory();

void setup() {
  // Initialize serial ports
  Serial.begin(115200);   // Debug Output / USB
  Serial1.begin(115200);  // ESP8266 Socket Hub
  Serial2.begin(115200);  // ESP8266 Door Hub (PCA9685)
  // Serial3 - REMOVED (No longer needed)

  delay(2000);

  Serial.println("\n=== ARDUINO MEGA GARDEN HUB v3.0.0 (INTEGRATED SENSORS & DISPLAY) ===");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Features: " + String(TOTAL_DEVICES) + " doors + Garden sensors + LCD I2C + OLED I2C + RGB + " + String(TOTAL_RELAY_DEVICES) + " relays");
  Serial.println("Interfaces:");
  Serial.println("  Serial0: Debug Output / USB");
  Serial.println("  Serial1: ESP8266 Socket Hub");
  Serial.println("  Serial2: ESP8266 Door Hub (PCA9685)");
  Serial.println("  I2C Bus: LCD 16x2 (0x27) + OLED 0.96\" (0x3C)");
  Serial.println("  Integrated: Garden sensors, RGB LED, Relay status LEDs");

  Wire.begin(); // Initialize I2C
  
  initializeRGB();
  initializeRelays();
  initializeLCD();
  initializeOLED();
  initializeSensors();
  initializeDevices();
  initializeTouchSensors();

  Serial.println("[INIT] ✓ Mega Garden Hub Ready (Integrated System)");
  Serial.println("==================================================================\n");

  systemUptime = millis();
}

void initializeRGB() {
  Serial.println("[RGB] Initializing garden status RGB LED...");
  
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  
  setRGBColor(255, 0, 0); delay(300);
  setRGBColor(0, 255, 0); delay(300);
  setRGBColor(0, 0, 255); delay(300);
  setRGBColor(0, 0, 0);
  
  gardenData.rgbStatus = "Initialized";
  Serial.println("[RGB] ✓ Garden status LED ready");
}

void initializeRelays() {
  Serial.println("[RELAY] Initializing 8-channel relay module (Active HIGH)...");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    pinMode(relayDevices[i].relayPin, OUTPUT);
    digitalWrite(relayDevices[i].relayPin, LOW);  // Active HIGH - OFF state
    
    pinMode(relayDevices[i].statusLedPin, OUTPUT);
    digitalWrite(relayDevices[i].statusLedPin, LOW);  // LED OFF
    
    relayDevices[i].isOn = false;
    relayDevices[i].lastToggle = 0;
    relayDevices[i].status = "off";
    
    Serial.println("[RELAY] " + relayDevices[i].deviceName + 
                   " (Pin " + String(relayDevices[i].relayPin) + 
                   ", LED " + String(relayDevices[i].statusLedPin) + "): " + 
                   relayDevices[i].serialNumber);
  }
  
  Serial.println("[RELAY] ✓ All 8 relays initialized (Active HIGH, OFF state)");
}

void initializeLCD() {
  Serial.println("[LCD] Initializing 16x2 LCD I2C display...");
  
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  // Startup message
  lcd.setCursor(0, 0);
  lcd.print("MEGA GARDEN HUB");
  lcd.setCursor(0, 1);
  lcd.print("v3.0.0 Starting");
  
  delay(2000);
  lcd.clear();
  
  gardenData.lcdPage = 0;
  gardenData.lastLcdUpdate = 0;
  gardenData.lastPageChange = millis();
  
  Serial.println("[LCD] ✓ 16x2 LCD I2C ready (Address: 0x" + String(LCD_I2C_ADDRESS, HEX) + ")");
}

void initializeOLED() {
  Serial.println("[OLED] Initializing 0.96\" OLED I2C display...");
  
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println("[OLED] ✗ SSD1306 allocation failed!");
    return;
  }
  
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  
  // Startup message
  oled.setCursor(0, 0);
  oled.setTextSize(2);
  oled.println("MEGA HUB");
  oled.setTextSize(1);
  oled.println("Garden System");
  oled.println("v3.0.0");
  oled.println("");
  oled.println("OLED Ready...");
  oled.display();
  
  delay(2000);
  oled.clearDisplay();
  
  gardenData.oledPage = 0;
  gardenData.lastOledUpdate = 0;
  
  Serial.println("[OLED] ✓ 0.96\" OLED I2C ready (Address: 0x" + String(OLED_I2C_ADDRESS, HEX) + ")");
}

void initializeSensors() {
  Serial.println("[SENSORS] Initializing garden sensors...");

  dht.begin();
  Serial.println("[SENSORS] ✓ DHT11 initialized");

  if (!rtc.begin()) {
    Serial.println("[SENSORS] ✗ RTC not found");
  } else {
    Serial.println("[SENSORS] ✓ RTC initialized");
  }

  pinMode(PUMP_RELAY_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN, LOW);
  pinMode(WATER_SENSOR_PIN, INPUT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);
  pinMode(SOIL_MOISTURE_PIN, INPUT);

  gardenData.temperature = 0;
  gardenData.humidity = 0;
  gardenData.soilMoisture = 0;
  gardenData.lightLevel = 0;
  gardenData.rainDetected = false;
  gardenData.pumpRunning = false;
  gardenData.currentTime = "00:00:00";
  gardenData.rgbStatus = "Offline";
  gardenData.fireDetected = false;
  gardenData.lastUpdated = 0;

  Serial.println("[SENSORS] ✓ All garden sensors integrated into Mega");
}

void initializeDevices() {
  Serial.println("[INIT] Initializing ESP8266 Door Hub device database...");

  for (int i = 0; i < TOTAL_DEVICES; i++) {
    devices[i].lastAction = "none";
    devices[i].doorOpen = false;
    devices[i].servoAngle = 0;
    devices[i].isOnline = false;
    devices[i].lastSeen = 0;
    devices[i].status = "offline";

    Serial.println("Door " + String(i + 1) + " (Ch" + String(devices[i].pwmChannel) + "): " + 
                   devices[i].serialNumber + " [" + devices[i].doorType + "]");
  }

  Serial.println("[INIT] ✓ " + String(TOTAL_DEVICES) + " doors for PCA9685 control");
}

void initializeTouchSensors() {
  Serial.println("[TOUCH] Initializing local touch sensors (Future Implementation)...");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].localControl) {
      pinMode(relayDevices[i].touchPin, INPUT_PULLUP);
      relayDevices[i].lastTouchTime = 0;
      
      Serial.println("[TOUCH] " + relayDevices[i].deviceName + 
                     " touch sensor (Pin " + String(relayDevices[i].touchPin) + ") ready");
    }
  }
  
  Serial.println("[TOUCH] ✓ Touch sensors ready for local relay control");
}

void loop() {
  // Handle serial communications
  if (Serial1.available()) {
    String socketMessage = Serial1.readStringUntil('\n');
    handleSocketHubMessage(socketMessage);
  }

  if (Serial2.available()) {
    String doorMessage = Serial2.readStringUntil('\n');
    handleDoorHubMessage(doorMessage);
  }

  // Sensor reading and automation
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 15000) { // Read every 15 seconds
    readGardenSensors();
    processGardenAutomation();
    lastSensorRead = millis();
  }

  // RGB LED updates
  static unsigned long lastRGBCheck = 0;
  if (millis() - lastRGBCheck > 1000) {
    updateRGBStatus();
    lastRGBCheck = millis();
  }

  // LCD Display updates
  static unsigned long lastLCDCheck = 0;
  if (millis() - lastLCDCheck > 500) { // Update LCD every 500ms
    updateLCDDisplay();
    lastLCDCheck = millis();
  }

  // OLED Display updates
  static unsigned long lastOLEDCheck = 0;
  if (millis() - lastOLEDCheck > 300) { // Update OLED every 300ms
    updateOLEDDisplay();
    lastOLEDCheck = millis();
  }

  // Relay status LED updates
  static unsigned long lastLEDCheck = 0;
  if (millis() - lastLEDCheck > 200) {
    updateRelayStatusLEDs();
    lastLEDCheck = millis();
  }

  // Local touch sensor handling
  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck > 100) {
    handleLocalTouchSensors();
    lastTouchCheck = millis();
  }

  // Fire alarm check
  static unsigned long lastFireCheck = 0;
  if (millis() - lastFireCheck > 5000) {
    checkFireAlarm();
    lastFireCheck = millis();
  }

  // Relay status broadcast
  static unsigned long lastRelayBroadcast = 0;
  if (millis() - lastRelayBroadcast > 60000) {
    sendRelayStatusToSocket();
    lastRelayBroadcast = millis();
  }

  checkPumpSafety();

  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 30000) {
    checkSystemHealth();
    lastStatusCheck = millis();
  }

  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 120000) {
    printSystemStatus();
    lastStatusPrint = millis();
  }

  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {
    sendSystemHeartbeat();
    lastHeartbeat = millis();
  }

  delay(50); // Reduced delay for better responsiveness
}

// ===== GARDEN SENSOR FUNCTIONS =====
void readGardenSensors() {
  sensorReadings++;

  gardenData.temperature = dht.readTemperature();
  gardenData.humidity = dht.readHumidity();

  if (isnan(gardenData.temperature) || isnan(gardenData.humidity)) {
    Serial.println("[SENSORS] ✗ DHT read failed, using defaults");
    if (isnan(gardenData.temperature)) gardenData.temperature = 25.0;
    if (isnan(gardenData.humidity)) gardenData.humidity = 50.0;
  }

  int soilRaw = analogRead(SOIL_MOISTURE_PIN);
  gardenData.soilMoisture = map(soilRaw, 1023, 0, 0, 100);

  bool lightDigital = digitalRead(LIGHT_SENSOR_PIN);
  gardenData.lightLevel = lightDigital ? 0 : 1000;

  int waterRaw = analogRead(WATER_SENSOR_PIN);
  gardenData.rainDetected = (waterRaw > 500);

  gardenData.fireDetected = (gardenData.temperature > 45);

  DateTime now = rtc.now();
  gardenData.currentTime = String(now.hour()) + ":" + 
                          (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":" + 
                          (now.second() < 10 ? "0" : "") + String(now.second());

  gardenData.lastUpdated = millis();

  Serial.println("[SENSORS] T:" + String(gardenData.temperature, 1) + "°C | " + 
                "H:" + String(gardenData.humidity, 1) + "% | " + 
                "S:" + String(gardenData.soilMoisture) + "% | " + 
                "L:" + String(gardenData.lightLevel) + " | " + 
                "R:" + String(gardenData.rainDetected ? "Y" : "N") + " | " +
                "F:" + String(gardenData.fireDetected ? "Y" : "N"));
}

void processGardenAutomation() {
  if (autoWateringEnabled) {
    if (gardenData.rainDetected && gardenData.pumpRunning) {
      stopPump("Rain detected");
    } else if (gardenData.soilMoisture < soilMoistureThreshold && !gardenData.pumpRunning && !gardenData.rainDetected) {
      startPump("Auto: Low soil moisture");
    } else if (gardenData.soilMoisture > (soilMoistureThreshold + 20) && gardenData.pumpRunning) {
      stopPump("Auto: Soil moisture sufficient");
    }
  }

  if (autoLightingEnabled) {
    processLightAutomation();
  }

  if (autoFanEnabled) {
    processFanAutomation();
  }
}

void processLightAutomation() {
  bool shouldLightBeOn = (gardenData.lightLevel < lightThreshold) && !gardenData.rainDetected;
  
  // Light 1 automation
  if (shouldLightBeOn && !relayDevices[2].isOn) {
    setRelayState(2, true, "Auto: Dark conditions");
  } else if (!shouldLightBeOn && relayDevices[2].isOn) {
    setRelayState(2, false, "Auto: Sufficient light");
  }
}

void processFanAutomation() {
  bool shouldFanBeOn = (gardenData.temperature > 30) || (gardenData.humidity > 80);
  
  if (shouldFanBeOn && !relayDevices[0].isOn) {
    setRelayState(0, true, "Auto: High temp/humidity");
  } else if (!shouldFanBeOn && relayDevices[0].isOn && gardenData.temperature < 27) {
    setRelayState(0, false, "Auto: Normal conditions");
  }
}

void startPump(String reason) {
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  gardenData.pumpRunning = true;
  pumpStartTime = millis();
  Serial.println("[PUMP] ✓ Started - " + reason);
}

void stopPump(String reason) {
  digitalWrite(PUMP_RELAY_PIN, LOW);
  gardenData.pumpRunning = false;
  pumpStartTime = 0;
  Serial.println("[PUMP] ✓ Stopped - " + reason);
}

void checkPumpSafety() {
  if (gardenData.pumpRunning && pumpStartTime > 0 && (millis() - pumpStartTime > maxPumpRunTime)) {
    stopPump("Safety timeout");
  }
}

// ===== LCD DISPLAY FUNCTIONS =====
void updateLCDDisplay() {
  // Change page every 3 seconds
  if (millis() - gardenData.lastPageChange > lcdPageDuration) {
    gardenData.lcdPage = (gardenData.lcdPage + 1) % LCD_PAGES;
    gardenData.lastPageChange = millis();
    lcd.clear();
  }

  // Update current page content
  switch (gardenData.lcdPage) {
    case 0: displayLCDPage1(); break;
    case 1: displayLCDPage2(); break;
    case 2: displayLCDPage3(); break;
    case 3: displayLCDPage4(); break;
  }
}

void displayLCDPage1() {
  // Page 1: Temperature, Humidity, Time
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(String(gardenData.temperature, 1));
  lcd.print("C H:");
  lcd.print(String(gardenData.humidity, 1));
  lcd.print("%");
  
  lcd.setCursor(0, 1);
  lcd.print("Time: ");
  lcd.print(gardenData.currentTime);
}

void displayLCDPage2() {
  // Page 2: Soil, Light, Pump status
  lcd.setCursor(0, 0);
  lcd.print("Soil:");
  lcd.print(String(gardenData.soilMoisture));
  lcd.print("% L:");
  lcd.print(String(gardenData.lightLevel));
  
  lcd.setCursor(0, 1);
  lcd.print("Pump:");
  lcd.print(gardenData.pumpRunning ? "ON " : "OFF");
  lcd.print(" Auto:");
  lcd.print(autoWateringEnabled ? "Y" : "N");
}

void displayLCDPage3() {
  // Page 3: Rain, Fire, Auto settings
  lcd.setCursor(0, 0);
  lcd.print("Rain:");
  lcd.print(gardenData.rainDetected ? "YES" : "NO ");
  lcd.print(" Fire:");
  lcd.print(gardenData.fireDetected ? "YES" : "NO");
  
  lcd.setCursor(0, 1);
  lcd.print("A-W:");
  lcd.print(autoWateringEnabled ? "Y" : "N");
  lcd.print(" A-L:");
  lcd.print(autoLightingEnabled ? "Y" : "N");
  lcd.print(" A-F:");
  lcd.print(autoFanEnabled ? "Y" : "N");
}

void displayLCDPage4() {
  // Page 4: System status, connections
  lcd.setCursor(0, 0);
  lcd.print("Sock:");
  lcd.print(socketHubConnected ? "Y" : "N");
  lcd.print(" Door:");
  lcd.print(doorHubConnected ? "Y" : "N");
  lcd.print(" RGB:");
  lcd.print(gardenData.rgbStatus.substring(0, 3));
  
  lcd.setCursor(0, 1);
  lcd.print("Up:");
  lcd.print(String((millis() - systemUptime) / 60000));
  lcd.print("m Free:");
  lcd.print(String(freeMemory()));
}

// ===== OLED DISPLAY FUNCTIONS =====
void updateOLEDDisplay() {
  // Change page every 2 seconds
  if (millis() - gardenData.lastOledUpdate > oledPageDuration) {
    gardenData.oledPage = (gardenData.oledPage + 1) % OLED_PAGES;
    gardenData.lastOledUpdate = millis();
    oled.clearDisplay();
  }

  // Update current page content
  switch (gardenData.oledPage) {
    case 0: displayOLEDPage1(); break;
    case 1: displayOLEDPage2(); break;
    case 2: displayOLEDPage3(); break;
  }
  
  oled.display();
}

void displayOLEDPage1() {
  // Page 1: Real-time sensor data with graphics
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("GARDEN SENSORS");
  
  // Temperature with bar graph
  oled.setCursor(0, 12);
  oled.print("Temp: ");
  oled.print(String(gardenData.temperature, 1));
  oled.print("C");
  
  // Temperature bar (0-50°C scale)
  int tempBar = map(gardenData.temperature, 0, 50, 0, 100);
  tempBar = constrain(tempBar, 0, 100);
  oled.drawRect(0, 22, 102, 6, SSD1306_WHITE);
  oled.fillRect(1, 23, tempBar, 4, SSD1306_WHITE);
  
  // Humidity
  oled.setCursor(0, 32);
  oled.print("Humidity: ");
  oled.print(String(gardenData.humidity, 1));
  oled.print("%");
  
  // Soil moisture with bar
  oled.setCursor(0, 42);
  oled.print("Soil: ");
  oled.print(String(gardenData.soilMoisture));
  oled.print("%");
  
  int soilBar = map(gardenData.soilMoisture, 0, 100, 0, 100);
  oled.drawRect(0, 52, 102, 6, SSD1306_WHITE);
  oled.fillRect(1, 53, soilBar, 4, SSD1306_WHITE);
  
  // Status icons on the right
  oled.setCursor(110, 12);
  oled.print(gardenData.rainDetected ? "R" : ".");
  oled.setCursor(110, 22);
  oled.print(gardenData.fireDetected ? "F" : ".");
  oled.setCursor(110, 32);
  oled.print(gardenData.pumpRunning ? "P" : ".");
}

void displayOLEDPage2() {
  // Page 2: Relay status visualization
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("RELAY STATUS");
  
  // Draw 8 relay boxes in 2 rows
  for (int i = 0; i < 8; i++) {
    int x = (i % 4) * 30;
    int y = 15 + (i / 4) * 20;
    
    // Draw relay box
    oled.drawRect(x, y, 25, 15, SSD1306_WHITE);
    
    // Fill if relay is ON
    if (relayDevices[i].isOn) {
      oled.fillRect(x + 1, y + 1, 23, 13, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }
    
    // Relay number
    oled.setCursor(x + 8, y + 4);
    oled.print(String(i + 1));
    
    oled.setTextColor(SSD1306_WHITE);
  }
  
  // Relay names at bottom
  oled.setCursor(0, 55);
  oled.setTextSize(1);
  String relayInfo = "";
  for (int i = 0; i < 8; i++) {
    if (relayDevices[i].isOn) {
      relayInfo += String(i + 1);
    }
  }
  oled.print("ON: " + (relayInfo.length() > 0 ? relayInfo : "None"));
}

void displayOLEDPage3() {
  // Page 3: Door status and system info
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("SYSTEM STATUS");
  
  // Connection status
  oled.setCursor(0, 12);
  oled.print("Socket: ");
  oled.println(socketHubConnected ? "OK" : "OFF");
  
  oled.setCursor(0, 22);
  oled.print("Doors:  ");
  oled.println(doorHubConnected ? "OK" : "OFF");
  
  // Door count
  int onlineDoors = 0;
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].isOnline) onlineDoors++;
  }
  
  oled.setCursor(0, 32);
  oled.print("Online: ");
  oled.print(String(onlineDoors));
  oled.print("/");
  oled.println(String(TOTAL_DEVICES));
  
  // Time and uptime
  oled.setCursor(0, 42);
  oled.print("Time: ");
  oled.println(gardenData.currentTime);
  
  oled.setCursor(0, 52);
  oled.print("Up: ");
  oled.print(String((millis() - systemUptime) / 60000));
  oled.print("m");
  
  // RGB status indicator (small circle)
  int rgbX = 100;
  int rgbY = 52;
  oled.fillCircle(rgbX, rgbY, 4, SSD1306_WHITE);
  if (currentGardenStatus == STATUS_GOOD) {
    // Draw a dot in center for "green"
    oled.fillCircle(rgbX, rgbY, 2, SSD1306_BLACK);
  }
}

// ===== RELAY STATUS LED FUNCTIONS =====
void updateRelayStatusLEDs() {
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].isOn) {
      // Blink LED when relay is ON
      bool blinkState = (millis() / 250) % 2;
      digitalWrite(relayDevices[i].statusLedPin, blinkState ? HIGH : LOW);
    } else {
      digitalWrite(relayDevices[i].statusLedPin, LOW);
    }
  }
}

// ===== TOUCH SENSOR FUNCTIONS =====
void handleLocalTouchSensors() {
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].localControl) {
      checkTouchSensor(i);
    }
  }
}

void checkTouchSensor(int relayIndex) {
  if (relayIndex < 0 || relayIndex >= TOTAL_RELAY_DEVICES) return;
  
  bool touchState = !digitalRead(relayDevices[relayIndex].touchPin); // Inverted for pullup
  static bool lastTouchStates[TOTAL_RELAY_DEVICES] = {false};
  
  // Detect rising edge (touch pressed)
  if (touchState && !lastTouchStates[relayIndex]) {
    unsigned long touchTime = millis();
    
    // Debounce - ignore touches within 500ms
    if (touchTime - relayDevices[relayIndex].lastTouchTime > 500) {
      toggleRelay(relayIndex, "Local touch control");
      relayDevices[relayIndex].lastTouchTime = touchTime;
      localTouchCommands++;
      
      Serial.println("[TOUCH] " + relayDevices[relayIndex].deviceName + " toggled locally");
    }
  }
  
  lastTouchStates[relayIndex] = touchState;
}

// ===== RGB FUNCTIONS =====
void updateRGBStatus() {
  GardenStatus newStatus = determineGardenStatus();
  
  if (newStatus != currentGardenStatus) {
    currentGardenStatus = newStatus;
    lastRGBUpdate = millis();
  }
  
  switch(currentGardenStatus) {
    case STATUS_GOOD:
      setRGBColor(0, 255, 0);
      gardenData.rgbStatus = "Green-Good";
      break;
    case STATUS_WATERING:
      setRGBColor(0, 0, 255);
      gardenData.rgbStatus = "Blue-Watering";
      break;
    case STATUS_DRY:
      setRGBColor(255, 255, 0);
      gardenData.rgbStatus = "Yellow-Dry";
      break;
    case STATUS_ERROR:
      setRGBColor(255, 0, 0);
      gardenData.rgbStatus = "Red-Error";
      break;
    case STATUS_RAIN:
      setRGBColor(128, 0, 128);
      gardenData.rgbStatus = "Purple-Rain";
      break;
    case STATUS_NIGHT:
      setRGBColor(64, 64, 64);
      gardenData.rgbStatus = "White-Night";
      break;
    case STATUS_OFFLINE:
      if (millis() - lastRGBUpdate > 500) {
        rgbBlinkState = !rgbBlinkState;
        setRGBColor(rgbBlinkState ? 255 : 0, 0, 0);
        gardenData.rgbStatus = "Red-Blink-Offline";
        lastRGBUpdate = millis();
      }
      break;
  }
}

GardenStatus determineGardenStatus() {
  if (gardenData.fireDetected) return STATUS_ERROR;
  if (!socketHubConnected && !doorHubConnected) return STATUS_OFFLINE;
  if (gardenData.rainDetected) return STATUS_RAIN;
  if (gardenData.pumpRunning) return STATUS_WATERING;
  if (gardenData.soilMoisture < soilMoistureThreshold) return STATUS_DRY;
  if (gardenData.lightLevel < lightThreshold) return STATUS_NIGHT;
  if (gardenData.temperature > 35 || gardenData.temperature < 0) return STATUS_ERROR;
  return STATUS_GOOD;
}

void setRGBColor(int red, int green, int blue) {
  analogWrite(RGB_RED_PIN, red);
  analogWrite(RGB_GREEN_PIN, green);
  analogWrite(RGB_BLUE_PIN, blue);
}

void handleRGBTest() {
  Serial.println("[RGB] Running test sequence...");
  gardenData.rgbStatus = "Testing";
  
  setRGBColor(255, 0, 0); delay(500);
  setRGBColor(0, 255, 0); delay(500);
  setRGBColor(0, 0, 255); delay(500);
  setRGBColor(255, 255, 0); delay(500);
  setRGBColor(128, 0, 128); delay(500);
  setRGBColor(64, 64, 64); delay(500);
  
  updateRGBStatus();
  Serial.println("[RGB] ✓ Test complete");
}

// ===== RELAY CONTROL FUNCTIONS =====
void toggleRelay(int deviceIndex, String reason) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  relayDevices[deviceIndex].isOn = !relayDevices[deviceIndex].isOn;
  digitalWrite(relayDevices[deviceIndex].relayPin, relayDevices[deviceIndex].isOn ? HIGH : LOW); // Active HIGH
  relayDevices[deviceIndex].lastToggle = millis();
  relayDevices[deviceIndex].status = relayDevices[deviceIndex].isOn ? "on" : "off";
  
  relayCommands++;
  
  Serial.println("[RELAY] " + relayDevices[deviceIndex].deviceName + " TOGGLED to " + 
                String(relayDevices[deviceIndex].isOn ? "ON" : "OFF") + " - " + reason);
  
  sendRelayDeviceStatus(deviceIndex);
}

void setRelayState(int deviceIndex, bool state, String reason) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  relayDevices[deviceIndex].isOn = state;
  digitalWrite(relayDevices[deviceIndex].relayPin, state ? HIGH : LOW); // Active HIGH
  relayDevices[deviceIndex].lastToggle = millis();
  relayDevices[deviceIndex].status = state ? "on" : "off";
  
  relayCommands++;
  
  Serial.println("[RELAY] " + relayDevices[deviceIndex].deviceName + " SET to " + 
                String(state ? "ON" : "OFF") + " - " + reason);
  
  sendRelayDeviceStatus(deviceIndex);
}

int findRelayDeviceBySerial(String serialNumber) {
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

void handleRelayCommand(String serialNumber, String action) {
  int deviceIndex = findRelayDeviceBySerial(serialNumber);
  
  if (deviceIndex < 0) {
    Serial.println("[RELAY] ✗ Device not found: " + serialNumber);
    sendRelayResponse(serialNumber, action, false, "Device not found");
    return;
  }
  
  bool success = false;
  String result = "ERR";
  
  if (action == "TOGGLE" && relayDevices[deviceIndex].canToggle) {
    toggleRelay(deviceIndex, "Remote toggle command");
    success = true;
    result = relayDevices[deviceIndex].isOn ? "ON" : "OFF";
    
  } else if (action == "ON") {
    setRelayState(deviceIndex, true, "Remote ON command");
    success = true;
    result = "ON";
    
  } else if (action == "OFF") {
    setRelayState(deviceIndex, false, "Remote OFF command");
    success = true;
    result = "OFF";
    
    if (deviceIndex == 1) { // Alarm device
      relayDevices[deviceIndex].fireOverride = true;
      Serial.println("[ALARM] Fire override enabled - manual control");
    }
    
  } else if (action == "RESET_OVERRIDE" && deviceIndex == 1) {
    relayDevices[deviceIndex].fireOverride = false;
    success = true;
    result = "OVERRIDE_RESET";
    Serial.println("[ALARM] Fire override reset - automatic control restored");
    
  } else if (action == "STATUS") {
    success = true;
    result = relayDevices[deviceIndex].isOn ? "ON" : "OFF";
  }
  
  sendRelayResponse(serialNumber, action, success, result);
}

void checkFireAlarm() {
  static bool lastFireState = false;
  
  if (gardenData.fireDetected && !lastFireState) {
    int alarmIndex = 1; // Alarm is relay device index 1
    if (!relayDevices[alarmIndex].fireOverride) {
      setRelayState(alarmIndex, true, "Fire detected");
      Serial.println("[FIRE] 🔥 ALARM ACTIVATED - Fire detected!");
    }
  }
  
  lastFireState = gardenData.fireDetected;
}

void sendRelayResponse(String serialNumber, String action, bool success, String result) {
  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"result\":\"" + result + "\",";
  json += "\"deviceId\":\"" + serialNumber + "\",";
  json += "\"command\":\"" + action + "\",";
  json += "\"type\":\"relay\",";
  json += "\"mega_processed\":true,";
  json += "\"timestamp\":" + String(millis());
  json += "}";

  Serial1.println("RESP:" + json);
  Serial.println("[MEGA→SOCKET] RELAY Response: " + json);
}

void sendRelayDeviceStatus(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  String statusMsg = "STS:RELAY:{";
  statusMsg += "\"deviceId\":\"" + relayDevices[deviceIndex].serialNumber + "\",";
  statusMsg += "\"name\":\"" + relayDevices[deviceIndex].deviceName + "\",";
  statusMsg += "\"state\":\"" + String(relayDevices[deviceIndex].isOn ? "on" : "off") + "\",";
  statusMsg += "\"pin\":" + String(relayDevices[deviceIndex].relayPin) + ",";
  statusMsg += "\"ledPin\":" + String(relayDevices[deviceIndex].statusLedPin) + ",";
  statusMsg += "\"touchPin\":" + String(relayDevices[deviceIndex].touchPin) + ",";
  statusMsg += "\"localControl\":" + String(relayDevices[deviceIndex].localControl ? "true" : "false") + ",";
  statusMsg += "\"lastToggle\":" + String(relayDevices[deviceIndex].lastToggle) + ",";
  
  if (deviceIndex == 1) {
    statusMsg += "\"fireOverride\":" + String(relayDevices[deviceIndex].fireOverride ? "true" : "false") + ",";
    statusMsg += "\"fireDetected\":" + String(gardenData.fireDetected ? "true" : "false") + ",";
  }
  
  statusMsg += "\"timestamp\":" + String(millis());
  statusMsg += "}";
  
  Serial1.println(statusMsg);
}

void sendRelayStatusToSocket() {
  Serial.println("[RELAY] Broadcasting status to socket...");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    sendRelayDeviceStatus(i);
    delay(50);
  }
}

// ===== DOOR HUB MESSAGE HANDLING =====
void handleDoorHubMessage(String message) {
  if (message.length() == 0) return;
  
  message.trim();
  
  doorHubConnected = true;
  lastDoorMessage = millis();
  
  Serial.println("[ESP8266_DOOR_HUB→MEGA] " + message);
  
  if (message.startsWith("DOOR_HUB_PCA9685_READY")) {
    Serial.println("[DOOR_HUB] ✓ ESP8266 Door Hub with PCA9685 is ready");
    
  } else if (message.startsWith("CHANNELS:")) {
    String channelInfo = message.substring(9);
    Serial.println("[DOOR_HUB] ✓ PCA9685 Channels: " + channelInfo);
    
  } else if (message.startsWith("RESP:")) {
    handleDoorHubResponse(message);
    
  } else if (message.startsWith("STS:")) {
    Serial1.println(message);
    Serial.println("[MEGA→SOCKET] " + message);
    
  } else if (message.startsWith("PCA9685_INFO:")) {
    Serial.println("[DOOR_HUB] PCA9685 Info: " + message);
    
  } else if (message.startsWith("STATUS_COMPLETE")) {
    Serial.println("[DOOR_HUB] ✓ Status update complete");
    
  } else if (message.startsWith("TEST_COMPLETE")) {
    Serial.println("[DOOR_HUB] ✓ Test sequence complete");
    
  } else if (message.startsWith("[")) {
    Serial.println("[DOOR_HUB_DEBUG] " + message);
    
  } else {
    Serial.println("[DOOR_HUB] Unknown: " + message);
  }
}

void handleDoorHubResponse(String respMessage) {
  if (!respMessage.startsWith("RESP:")) return;
  
  String jsonData = respMessage.substring(5);
  
  String deviceId = extractJsonField(jsonData, "d");
  String success = extractJsonField(jsonData, "s");
  String result = extractJsonField(jsonData, "r");
  String channel = extractJsonField(jsonData, "ch");
  
  int deviceIndex = findDeviceBySerial(deviceId);
  if (deviceIndex >= 0) {
    devices[deviceIndex].isOnline = true;
    devices[deviceIndex].lastSeen = millis();
    devices[deviceIndex].status = (success == "1") ? "online" : "error";
    
    String angle = extractJsonField(jsonData, "a");
    if (angle.length() > 0) {
      devices[deviceIndex].servoAngle = angle.toInt();
      devices[deviceIndex].doorOpen = (devices[deviceIndex].servoAngle > 45);
    }
    
    Serial.println("[DOOR_UPDATE] Door " + String(deviceIndex + 1) + " (Ch" + channel + "): " + 
                   result + " at " + String(devices[deviceIndex].servoAngle) + "°");
  }
  
  Serial1.println(respMessage);
  responsesForwarded++;
  
  Serial.println("[MEGA→SOCKET] Door response forwarded");
}

// ===== SOCKET HUB MESSAGE HANDLING =====
void handleSocketHubMessage(String message) {
  if (message.length() == 0) return;
  
  message.trim();
  
  socketHubConnected = true;
  lastSocketMessage = millis();

  if (message.startsWith("CMD:")) {
    String cmdData = message.substring(4);
    
    if (cmdData.indexOf("GARDEN") >= 0) {
      handleGardenSocketCommand(message);
    } else if (cmdData.indexOf("RELAY") >= 0) {
      handleRelaySocketCommand(message);
    } else {
      int colonIndex = cmdData.indexOf(':');
      if (colonIndex > 0 && colonIndex < cmdData.length() - 1) {
        String serialNumber = cmdData.substring(0, colonIndex);
        String action = cmdData.substring(colonIndex + 1);
        
        if (serialNumber.length() == 32 && serialNumber.startsWith("SERL")) {
          forwardCommand(serialNumber, action);
        }
      }
    }
  }
}

void handleGardenSocketCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4);

  if (cmdData == "GARDEN:PUMP_ON") {
    startPump("Remote command");
  } else if (cmdData == "GARDEN:PUMP_OFF") {
    stopPump("Remote command");
  } else if (cmdData == "GARDEN:AUTO_WATER_TOGGLE") {
    autoWateringEnabled = !autoWateringEnabled;
    Serial.println("[GARDEN] Auto watering: " + String(autoWateringEnabled ? "ON" : "OFF"));
  } else if (cmdData == "GARDEN:AUTO_LIGHT_TOGGLE") {
    autoLightingEnabled = !autoLightingEnabled;
    Serial.println("[GARDEN] Auto lighting: " + String(autoLightingEnabled ? "ON" : "OFF"));
  } else if (cmdData == "GARDEN:AUTO_FAN_TOGGLE") {
    autoFanEnabled = !autoFanEnabled;
    Serial.println("[GARDEN] Auto fan: " + String(autoFanEnabled ? "ON" : "OFF"));
  } else if (cmdData == "GARDEN:RGB_TEST") {
    handleRGBTest();
  } else if (cmdData == "GARDEN:LCD_TEST") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LCD TEST MODE");
    lcd.setCursor(0, 1);
    lcd.print("All Systems OK");
    delay(2000);
    lcd.clear();
  } else if (cmdData == "GARDEN:OLED_TEST") {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("OLED TEST MODE");
    oled.println("");
    oled.setTextSize(2);
    oled.println("MEGA HUB");
    oled.setTextSize(1);
    oled.println("");
    oled.println("Graphics Test:");
    
    // Draw some test graphics
    for (int i = 0; i < 128; i += 8) {
      oled.drawPixel(i, 50, SSD1306_WHITE);
    }
    oled.drawRect(0, 55, 128, 8, SSD1306_WHITE);
    oled.display();
    delay(3000);
    oled.clearDisplay();
  }

  String response = "RESP:{\"success\":true,\"command\":\"" + cmdData + "\",\"type\":\"garden\",\"rgb_status\":\"" + gardenData.rgbStatus + "\"}";
  Serial1.println(response);
}

void handleRelaySocketCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4);
  
  int colonIndex = cmdData.indexOf(':');
  if (colonIndex <= 0) {
    Serial.println("[RELAY] ✗ Invalid relay format: " + cmdMessage);
    return;
  }
  
  String serialNumber = cmdData.substring(0, colonIndex);
  String action = cmdData.substring(colonIndex + 1);
  
  if (serialNumber.length() != 32 || !serialNumber.startsWith("RELAY")) {
    Serial.println("[RELAY] ✗ Invalid relay serial: " + serialNumber);
    sendRelayResponse(serialNumber, action, false, "Invalid serial format");
    return;
  }
  
  Serial.println("[RELAY] Processing: " + serialNumber + " -> " + action);
  handleRelayCommand(serialNumber, action);
}

void forwardCommand(String serialNumber, String action) {
  Serial.println("\n[FORWARD] Processing command for ESP8266 Door Hub");
  Serial.println("[FORWARD] Serial: " + serialNumber);
  Serial.println("[FORWARD] Action: " + action);
  
  if (serialNumber.length() > 32) {
    Serial.println("[FORWARD] ✗ Serial too long: " + String(serialNumber.length()));
    sendErrorResponse(serialNumber, action, "Serial number too long");
    return;
  }
  
  int deviceIndex = findDeviceBySerial(serialNumber);
  if (deviceIndex < 0) {
    Serial.println("[FORWARD] ✗ Device not found");
    sendErrorResponse(serialNumber, action, "Device not found");
    return;
  }
  
  Serial.println("[FORWARD] Found device at index: " + String(deviceIndex) + 
                 " (PCA9685 Ch" + String(devices[deviceIndex].pwmChannel) + ")");
  
  devices[deviceIndex].lastAction = action;
  devices[deviceIndex].lastSeen = millis();
  
  String forwardCmd = "CMD:" + serialNumber + ":" + action;
  Serial2.println(forwardCmd);
  delay(10);
  
  commandsProcessed++;
  
  Serial.println("[FORWARD] ✓ Sent to ESP8266 Door Hub");
  Serial.println("[MEGA→ESP8266_DOOR_HUB] " + forwardCmd);
}

// ===== SYSTEM FUNCTIONS =====
int findDeviceBySerial(String serialNumber) {
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].serialNumber == serialNumber) {
      return i;
    }
  }
  return -1;
}

String extractJsonField(String json, String fieldName) {
  String searchKey = "\"" + fieldName + "\":";
  int startIndex = json.indexOf(searchKey);
  if (startIndex == -1) return "";

  startIndex += searchKey.length();
  while (startIndex < json.length() && (json.charAt(startIndex) == ' ' || json.charAt(startIndex) == '"')) {
    startIndex++;
  }

  int endIndex = startIndex;
  while (endIndex < json.length() && json.charAt(endIndex) != ',' && json.charAt(endIndex) != '}' && json.charAt(endIndex) != '"') {
    endIndex++;
  }

  return json.substring(startIndex, endIndex);
}

void sendErrorResponse(String serialNumber, String action, String error) {
  String json = "{";
  json += "\"success\":false,";
  json += "\"result\":\"" + error + "\",";
  json += "\"deviceId\":\"" + serialNumber + "\",";
  json += "\"command\":\"" + action + "\",";
  json += "\"mega_processed\":true,";
  json += "\"timestamp\":" + String(millis());
  json += "}";

  Serial1.println("RESP:" + json);
}

void checkSystemHealth() {
  if (socketHubConnected && (millis() - lastSocketMessage > 120000)) {
    socketHubConnected = false;
    Serial.println("[HEALTH] ✗ Socket Hub timeout");
  }

  if (doorHubConnected && (millis() - lastDoorMessage > 120000)) {
    doorHubConnected = false;
    Serial.println("[HEALTH] ✗ ESP8266 Door Hub timeout");
  }

  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].isOnline && (millis() - devices[i].lastSeen > 300000)) {
      devices[i].isOnline = false;
      devices[i].status = "timeout";
      Serial.println("[HEALTH] ✗ Door " + String(i + 1) + " timeout");
    }
  }
}

void sendSystemHeartbeat() {
  Serial.println("[HEARTBEAT] Mega Garden Hub (Integrated) - uptime: " + String((millis() - systemUptime) / 1000) + "s");
  Serial.println("           Sensors: " + String(sensorReadings) + " readings");
  Serial.println("           Garden: " + String(gardenData.temperature, 1) + "°C, " + 
                 String(gardenData.soilMoisture) + "% soil, pump " + 
                 String(gardenData.pumpRunning ? "ON" : "OFF"));
  Serial.println("           RGB: " + gardenData.rgbStatus + " | LCD Page: " + String(gardenData.lcdPage + 1) + " | OLED Page: " + String(gardenData.oledPage + 1));
  Serial.println("           Relays: " + String(relayCommands) + " remote + " + String(localTouchCommands) + " local commands");
  Serial.println("           Doors: ESP8266 + PCA9685 (" + String(TOTAL_DEVICES) + " channels)");
  Serial.println("           Fire: " + String(gardenData.fireDetected ? "DETECTED" : "NONE"));
}

void printSystemStatus() {
  Serial.println("\n======= MEGA GARDEN HUB STATUS (INTEGRATED SYSTEM) =======");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Version: " + String(FIRMWARE_VERSION));
  Serial.println("Uptime: " + String((millis() - systemUptime) / 1000) + " seconds");
  Serial.println("Commands: " + String(commandsProcessed) + " | Responses: " + String(responsesForwarded));
  Serial.println("Sensor Readings: " + String(sensorReadings) + " | Relay Commands: " + String(relayCommands));
  Serial.println("Local Touch Commands: " + String(localTouchCommands));

  Serial.println("\n--- Hub Connections ---");
  Serial.println("Socket Hub: " + String(socketHubConnected ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("Door Hub (ESP8266 + PCA9685): " + String(doorHubConnected ? "CONNECTED" : "DISCONNECTED"));

  Serial.println("\n--- Integrated Garden System ---");
  Serial.println("Temperature: " + String(gardenData.temperature, 1) + "°C");
  Serial.println("Humidity: " + String(gardenData.humidity, 1) + "%");
  Serial.println("Soil Moisture: " + String(gardenData.soilMoisture) + "%");
  Serial.println("Light Level: " + String(gardenData.lightLevel));
  Serial.println("Rain: " + String(gardenData.rainDetected ? "DETECTED" : "NONE"));
  Serial.println("Fire: " + String(gardenData.fireDetected ? "DETECTED" : "NONE"));
  Serial.println("Pump: " + String(gardenData.pumpRunning ? "RUNNING" : "STOPPED"));
  Serial.println("RGB Status: " + gardenData.rgbStatus);
  Serial.println("LCD Page: " + String(gardenData.lcdPage + 1) + "/" + String(LCD_PAGES) + " (I2C 0x" + String(LCD_I2C_ADDRESS, HEX) + ")");
  Serial.println("OLED Page: " + String(gardenData.oledPage + 1) + "/" + String(OLED_PAGES) + " (I2C 0x" + String(OLED_I2C_ADDRESS, HEX) + ")");

  Serial.println("\n--- Automation Settings ---");
  Serial.println("Auto Watering: " + String(autoWateringEnabled ? "ENABLED" : "DISABLED"));
  Serial.println("Auto Lighting: " + String(autoLightingEnabled ? "ENABLED" : "DISABLED"));
  Serial.println("Auto Fan: " + String(autoFanEnabled ? "ENABLED" : "DISABLED"));

  Serial.println("\n--- 8-Channel Relay Status (Active HIGH) ---");
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    Serial.println("Relay " + String(i + 1) + " (" + relayDevices[i].deviceName + "): " + 
                   String(relayDevices[i].isOn ? "ON" : "OFF") + 
                   " | Pin " + String(relayDevices[i].relayPin) + 
                   " | LED " + String(relayDevices[i].statusLedPin) + 
                   " | Touch " + String(relayDevices[i].touchPin) +
                   " | Local: " + String(relayDevices[i].localControl ? "Y" : "N"));
    
    if (i == 1) {
      Serial.println("  └─ Fire Override: " + String(relayDevices[i].fireOverride ? "YES" : "NO"));
    }
  }

  Serial.println("\n--- Door Devices (ESP8266 + PCA9685) ---");
  int onlineCount = 0;
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    if (devices[i].isOnline) onlineCount++;
    Serial.println("Door " + String(i + 1) + " (Ch" + String(devices[i].pwmChannel) + "): " + 
                   String(devices[i].isOnline ? "ONLINE" : "OFFLINE") + " | " + 
                   String(devices[i].doorOpen ? "OPEN" : "CLOSED") + " | " + 
                   String(devices[i].servoAngle) + "° | " + devices[i].status + " | " +
                   devices[i].doorType);
  }

  Serial.println("\nOnline Doors: " + String(onlineCount) + "/" + String(TOTAL_DEVICES));
  Serial.println("Free RAM: " + String(freeMemory()) + " bytes");
  Serial.println("==========================================================\n");
}

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}