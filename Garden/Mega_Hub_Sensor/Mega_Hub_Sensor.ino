// ========================================
// ARDUINO MEGA GARDEN HUB v3.3.0 - 4x4 KEYPAD VERSION
// ========================================
#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

#define FIRMWARE_VERSION "3.3.0"
#define HUB_ID "MEGA_HUB_GARDEN_KEYPAD"

// ===== ARDUINO MEGA CENTRAL HUB =====
// Serial0 (0,1): Debug Output / USB
// Serial1 (18,19): ESP8266 Socket Hub (Garden Gateway)

// ===== GARDEN SENSOR PINS =====
#define DHT_PIN 22
#define DHT_TYPE DHT11
#define WATER_SENSOR_PIN A0
#define LIGHT_SENSOR_PIN 24
#define SOIL_MOISTURE_PIN A2

// ===== RGB LED PINS (Garden Status) =====
#define RGB_RED_PIN 5
#define RGB_GREEN_PIN 6
#define RGB_BLUE_PIN 7

// ===== I2C DISPLAY ADDRESSES =====
#define LCD_I2C_ADDRESS 0x27
#define OLED_I2C_ADDRESS 0x3C
#define RTC_I2C_ADDRESS 0x68  // DS3231 default
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// ===== 10 RELAY PINS =====
#define RELAY_1_PIN 30   // Fan (5V LOW)
#define RELAY_2_PIN 31   // Buzzer (5V LOW)
#define RELAY_3_PIN 32   // LED (5V LOW)
#define RELAY_4_PIN 33   // LED (5V LOW)
#define RELAY_5_PIN 34   // LED (5V LOW)
#define RELAY_6_PIN 35   // LED (5V LOW)
#define RELAY_7_PIN 36   // LED 220V (5V LOW)
#define RELAY_8_PIN 37   // LED 220V (5V LOW)
#define RELAY_9_PIN 23   // Pump (5V HIGH - separate board)
#define RELAY_10_PIN 38  // Reserve (5V LOW)

// ===== 4x4 KEYPAD CONFIGURATION =====
const byte KEYPAD_ROWS = 4;
const byte KEYPAD_COLS = 4;

// Keypad layout - maps to relay indices 0-9
char keypadLayout[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'1','2','3','A'},  // Keys 1,2,3 -> Relays 0,1,2
  {'4','5','6','B'},  // Keys 4,5,6 -> Relays 3,4,5
  {'7','8','9','C'},  // Keys 7,8,9 -> Relays 6,7,8
  {'*','0','#','D'}   // Key 0 -> Relay 9, *, #, A-D for special functions
};

// Keypad pin connections (update these to your actual wiring)
byte keypadRowPins[KEYPAD_ROWS] = {25, 26, 27, 28};    // Rows R1-R4
byte keypadColPins[KEYPAD_COLS] = {29, 44, 45, 46};    // Columns C1-C4

// Key mapping to relay indices
struct KeyMapping {
  char key;
  int relayIndex;
  String function;
};

KeyMapping keyMappings[] = {
  {'1', 0, "Fan"},
  {'2', 1, "Buzzer"},
  {'3', 2, "LED1"},
  {'4', 3, "LED2"},
  {'5', 4, "LED3"},
  {'6', 5, "LED4"},
  {'7', 6, "LED220V1"},
  {'8', 7, "LED220V2"},
  {'9', 8, "Pump"},
  {'0', 9, "Reserve"},
  {'A', -1, "Auto Water Toggle"},
  {'B', -1, "Auto Light Toggle"},
  {'C', -1, "Auto Fan Toggle"},
  {'D', -1, "Emergency Stop"},
  {'*', -1, "System Status"},
  {'#', -1, "All Relays Off"}
};

const int TOTAL_KEY_MAPPINGS = sizeof(keyMappings) / sizeof(KeyMapping);

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
Keypad keypad = Keypad(makeKeymap(keypadLayout), keypadRowPins, keypadColPins, KEYPAD_ROWS, KEYPAD_COLS);

// ===== RELAY DEVICE DATA =====
struct RelayDevice {
  String serialNumber;
  String deviceName;
  int relayPin;
  char keypadKey;
  bool isOn;
  bool canToggle;
  bool activeHigh;  // true for HIGH trigger, false for LOW trigger
  bool localControl;
  unsigned long lastToggle;
  unsigned long lastKeyPress;
  String status;
  bool powerSafeMode; // Prevent simultaneous high power relay activation
};

RelayDevice relayDevices[10] = {
  { "RELAY27JUN2501FAN001CONTROL001", "Fan", RELAY_1_PIN, '1', false, true, false, true, 0, 0, "off", false },
  { "RELAY27JUN2501BUZZER1CONTROL01", "Buzzer", RELAY_2_PIN, '2', false, true, false, true, 0, 0, "off", false },
  { "RELAY27JUN2501LED001CONTROL001", "LED1", RELAY_3_PIN, '3', false, true, false, true, 0, 0, "off", false },
  { "RELAY27JUN2501LED002CONTROL001", "LED2", RELAY_4_PIN, '4', false, true, false, true, 0, 0, "off", false },
  { "RELAY27JUN2501LED003CONTROL001", "LED3", RELAY_5_PIN, '5', false, true, false, true, 0, 0, "off", false },
  { "RELAY27JUN2501LED004CONTROL001", "LED4", RELAY_6_PIN, '6', false, true, false, true, 0, 0, "off", false },
  { "RELAY27JUN2501LED220V7CONTROL1", "LED220V1", RELAY_7_PIN, '7', false, true, false, true, 0, 0, "off", true },
  { "RELAY27JUN2501LED220V8CONTROL1", "LED220V2", RELAY_8_PIN, '8', false, true, false, true, 0, 0, "off", true },
  { "RELAY27JUN2501PUMP9CONTROL001", "Pump", RELAY_9_PIN, '9', false, true, true, true, 0, 0, "off", true },
  { "RELAY27JUN2501RESERVE10CTRL01", "Reserve", RELAY_10_PIN, '0', false, true, false, false, 0, 0, "off", false }
};

const int TOTAL_RELAY_DEVICES = 10;

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
  
  int lcdPage;
  int oledPage;
  unsigned long lastLcdUpdate;
  unsigned long lastOledUpdate;
  unsigned long lastPageChange;
};

GardenData gardenData;

// ===== SYSTEM STATUS =====
bool socketHubConnected = false;
unsigned long lastSocketMessage = 0;
bool systemStable = true;
unsigned long lastDisplayRefresh = 0;
bool rtcAvailable = false;
bool lcdAvailable = false;
bool oledAvailable = false;

// ===== AUTOMATION SETTINGS =====
int soilMoistureThreshold = 30;
int lightThreshold = 200;
int humidityThreshold = 85;
int nightTimeHour = 20;  // 8 PM
int dayTimeHour = 6;     // 6 AM
bool autoWateringEnabled = true;
bool autoLightingEnabled = true;
bool autoFanEnabled = true;
unsigned long pumpStartTime = 0;
const unsigned long maxPumpRunTime = 300000;

// ===== DYNAMIC EVENT SYSTEM =====
enum SystemPriority {
  PRIORITY_USER_COMMAND = 0,    // User commands (highest)
  PRIORITY_EMERGENCY = 1,       // Fire, flood
  PRIORITY_CRITICAL = 2,        // Pump safety, temperature
  PRIORITY_NORMAL = 3,          // Regular automation
  PRIORITY_LOW = 4              // Lighting, comfort
};

struct AutomationEvent {
  bool isActive;
  unsigned long lastCheck;
  unsigned long interval;
  SystemPriority priority;
};

AutomationEvent automationEvents[5] = {
  {true, 0, 500, PRIORITY_USER_COMMAND},     // User commands every 500ms
  {true, 0, 1000, PRIORITY_EMERGENCY},       // Emergency check every 1s
  {true, 0, 3000, PRIORITY_CRITICAL},        // Critical every 3s
  {true, 0, 5000, PRIORITY_NORMAL},          // Normal every 5s
  {true, 0, 10000, PRIORITY_LOW}             // Low priority every 10s
};

bool emergencyMode = false;
bool nightMode = false;
bool userCommandActive = false;
unsigned long lastUserCommand = 0;
const unsigned long USER_COMMAND_OVERRIDE_TIME = 300000; // 5 minutes
unsigned long lastEventCheck = 0;

// ===== RGB STATUS =====
GardenStatus currentGardenStatus = STATUS_OFFLINE;
unsigned long lastRGBUpdate = 0;
bool rgbBlinkState = false;

// ===== DISPLAY SETTINGS =====
const int LCD_PAGES = 4;
const int OLED_PAGES = 3;
unsigned long lcdPageDuration = 4000;
unsigned long oledPageDuration = 3000;

// ===== STATISTICS =====
unsigned long commandsProcessed = 0;
unsigned long sensorReadings = 0;
unsigned long relayCommands = 0;
unsigned long localKeyCommands = 0;
unsigned long systemUptime = 0;

// ===== KEYPAD VARIABLES =====
char lastKey = NO_KEY;
unsigned long lastKeyTime = 0;
const unsigned long KEY_DEBOUNCE_TIME = 200;

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  delay(2000); // Reduced startup delay

  // Initialize I2C first
  Wire.begin();
  Wire.setClock(100000);
  
  initializeRGB();
  delay(500);
  
  initializeRelays();
  delay(500);
  
  initializeKeypad();
  delay(500);
  
  initializeI2CDevices();
  delay(500);
  
  initializeSensors();
  
  // Force initial reads
  readGardenSensors();
  updateDisplays();
  
  systemUptime = millis();
}

void initializeRGB() {
  Serial.println("[RGB] Initializing garden status RGB LED...");
  
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  
  setRGBColor(255, 0, 0); delay(500);
  setRGBColor(0, 255, 0); delay(500);
  setRGBColor(0, 0, 255); delay(500);
  setRGBColor(0, 0, 0);
  
  gardenData.rgbStatus = "Initialized";
  Serial.println("[RGB] ✓ Garden status LED ready");
}

void initializeRelays() {
  Serial.println("[RELAY] Initializing 10 relays with power management...");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    pinMode(relayDevices[i].relayPin, OUTPUT);
    
    if (relayDevices[i].activeHigh) {
      digitalWrite(relayDevices[i].relayPin, LOW);
    } else {
      digitalWrite(relayDevices[i].relayPin, HIGH);
    }
    
    relayDevices[i].isOn = false;
    relayDevices[i].lastToggle = 0;
    relayDevices[i].status = "off";
    
    String triggerType = relayDevices[i].activeHigh ? "HIGH" : "LOW";
    Serial.println("[RELAY] " + relayDevices[i].deviceName + 
                   " (Pin " + String(relayDevices[i].relayPin) + 
                   ", Key '" + String(relayDevices[i].keypadKey) + 
                   "', Trigger " + triggerType + ")");
    
    delay(100);
  }
  
  Serial.println("[RELAY] ✓ All 10 relays initialized safely");
}

void initializeKeypad() {
  // Set keypad parameters
  keypad.setDebounceTime(50);
  keypad.setHoldTime(1000);
}

void initializeI2CDevices() {
  Serial.println("[I2C] Initializing I2C devices...");
  
  delay(200);
  if (rtc.begin()) {
    rtcAvailable = true;
    Serial.println("[RTC] ✓ DS3231 RTC detected");
    
    if (rtc.lostPower()) {
      Serial.println("[RTC] Lost power, setting time...");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    
    DateTime now = rtc.now();
    Serial.println("[RTC] Current time: " + String(now.year()) + "/" + 
                   String(now.month()) + "/" + String(now.day()) + " " +
                   String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()));
  } else {
    Serial.println("[RTC] ✗ DS3231 not found");
  }
  
  delay(300);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("MEGA GARDEN HUB");
  lcd.setCursor(0, 1);
  lcd.print("v3.3.0 KEYPAD");
  
  lcdAvailable = true;
  Serial.println("[LCD] ✓ 16x2 LCD I2C ready");
  
  delay(2000);
  lcd.clear();
  
  gardenData.lcdPage = 0;
  gardenData.lastLcdUpdate = 0;
  gardenData.lastPageChange = millis();
  
  delay(300);
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    oledAvailable = true;
    
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    
    oled.setCursor(0, 0);
    oled.setTextSize(2);
    oled.println("MEGA HUB");
    oled.setTextSize(1);
    oled.println("Keypad Ready!");
    oled.println("v3.3.0");
    oled.println("");
    oled.println("4x4 Matrix Input");
    oled.display();
    
    Serial.println("[OLED] ✓ 0.96\" OLED I2C ready");
    
    delay(2000);
    oled.clearDisplay();
    
    gardenData.oledPage = 0;
    gardenData.lastOledUpdate = 0;
  } else {
    Serial.println("[OLED] ✗ SSD1306 allocation failed");
  }
  
  Serial.println("[I2C] I2C devices initialization complete");
}

void initializeSensors() {
  Serial.println("[SENSORS] Initializing garden sensors...");

  dht.begin();
  Serial.println("[SENSORS] ✓ DHT11 initialized");

  pinMode(WATER_SENSOR_PIN, INPUT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);
  pinMode(SOIL_MOISTURE_PIN, INPUT);

  gardenData.temperature = 25.0;
  gardenData.humidity = 50.0;
  gardenData.soilMoisture = 50;
  gardenData.lightLevel = 500;
  gardenData.rainDetected = false;
  gardenData.pumpRunning = false;
  gardenData.currentTime = "00:00:00";
  gardenData.rgbStatus = "Offline";
  gardenData.fireDetected = false;
  gardenData.lastUpdated = 0;

  Serial.println("[SENSORS] ✓ All garden sensors integrated");
  
  readGardenSensors();
}

void loop() {
  if (Serial1.available()) {
    String socketMessage = Serial1.readStringUntil('\n');
    handleSocketHubMessage(socketMessage);
  }

  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 10000) {
    readGardenSensors();
    processGardenAutomation();
    lastSensorRead = millis();
  }

  static unsigned long lastRGBCheck = 0;
  if (millis() - lastRGBCheck > 1000) {
    updateRGBStatus();
    lastRGBCheck = millis();
  }

  // Handle keypad input (high frequency)
  static unsigned long lastKeyCheck = 0;
  if (millis() - lastKeyCheck > 10) {
    handleKeypadInput();
    lastKeyCheck = millis();
  }

  static unsigned long lastDisplayCheck = 0;
  if (millis() - lastDisplayCheck > 1000) {
    updateDisplays();
    lastDisplayCheck = millis();
  }

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

  delay(5);
}

// ===== KEYPAD FUNCTIONS =====
void handleKeypadInput() {
  char key = keypad.getKey();
  
  if (key != NO_KEY && key != lastKey && (millis() - lastKeyTime > KEY_DEBOUNCE_TIME)) {
    processKeyPress(key);
    lastKey = key;
    lastKeyTime = millis();
    localKeyCommands++;
  }
}

void processKeyPress(char key) {
  // Find key mapping
  for (int i = 0; i < TOTAL_KEY_MAPPINGS; i++) {
    if (keyMappings[i].key == key) {
      if (keyMappings[i].relayIndex >= 0) {
        // Relay control key
        int relayIndex = keyMappings[i].relayIndex;
        toggleRelayWithSafety(relayIndex, "Keypad: Key '" + String(key) + "'");
        relayDevices[relayIndex].lastKeyPress = millis();
      } else {
        // Special function key
        handleSpecialFunction(key, keyMappings[i].function);
      }
      return;
    }
  }
}

void handleSpecialFunction(char key, String function) {
  if (key == 'A') {
    autoWateringEnabled = !autoWateringEnabled;
  } else if (key == 'B') {
    autoLightingEnabled = !autoLightingEnabled;
  } else if (key == 'C') {
    autoFanEnabled = !autoFanEnabled;
  } else if (key == 'D') {
    emergencyStop();
  } else if (key == '*') {
    // Status check - send to socket only
    sendSocketStatus("status_request", "Manual status check");
  } else if (key == '#') {
    allRelaysOff();
  }
}

void emergencyStop() {
  Serial.println("[EMERGENCY] STOPPING ALL SYSTEMS!");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    setRelayState(i, false, "Emergency stop");
  }
  
  autoWateringEnabled = false;
  autoLightingEnabled = false;
  autoFanEnabled = false;
  
  setRGBColor(255, 0, 0); // Red alert
  
  Serial.println("[EMERGENCY] All systems stopped!");
}

void allRelaysOff() {
  Serial.println("[KEYPAD] Turning all relays OFF");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].isOn) {
      setRelayState(i, false, "All off command");
    }
  }
  
  Serial.println("[KEYPAD] ✓ All relays OFF");
}

void toggleRelayWithSafety(int relayIndex, String reason) {
  if (relayDevices[relayIndex].powerSafeMode && !relayDevices[relayIndex].isOn) {
    if (checkHighPowerRelaySafety(relayIndex)) {
      Serial.println("[SAFETY] Power safety prevented toggle: " + relayDevices[relayIndex].deviceName);
      return;
    }
  }
  
  toggleRelay(relayIndex, reason);
}

bool checkHighPowerRelaySafety(int relayIndex) {
  int activePowerRelays = 0;
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].powerSafeMode && relayDevices[i].isOn) {
      activePowerRelays++;
    }
  }
  
  if (activePowerRelays >= 2 && !relayDevices[relayIndex].isOn) {
    return true;
  }
  
  return false;
}

// ===== RTC TIME SETTING FUNCTIONS =====
void setRTCTime(int year, int month, int day, int hour, int minute, int second) {
  if (rtcAvailable) {
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
    Serial.println("[RTC] ✓ Time set to: " + String(year) + "/" + String(month) + "/" + String(day) + 
                   " " + String(hour) + ":" + String(minute) + ":" + String(second));
    
    // Verify setting
    DateTime now = rtc.now();
    Serial.println("[RTC] Verified time: " + String(now.year()) + "/" + String(now.month()) + "/" + 
                   String(now.day()) + " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()));
  } else {
    Serial.println("[RTC] ✗ RTC not available");
  }
}

// Set current Vietnam time (GMT+7)
void setVietnamTime() {
  // Example: January 15, 2025, 14:30:00 (2:30 PM)
  // Update these values to current time
  setRTCTime(2025, 1, 15, 14, 30, 0);
}

// Sync time via Serial command
void handleTimeCommand(String timeStr) {
  // Format: "TIME:YYYY,MM,DD,HH,MM,SS"
  if (timeStr.startsWith("TIME:")) {
    String timeData = timeStr.substring(5);
    
    int year = timeData.substring(0, 4).toInt();
    int month = timeData.substring(5, 7).toInt();
    int day = timeData.substring(8, 10).toInt();
    int hour = timeData.substring(11, 13).toInt();
    int minute = timeData.substring(14, 16).toInt();
    int second = timeData.substring(17, 19).toInt();
    
    setRTCTime(year, month, day, hour, minute, second);
  }
}
void readGardenSensors() {
  sensorReadings++;

  float tempReading = dht.readTemperature();
  float humReading = dht.readHumidity();
  
  if (!isnan(tempReading)) {
    gardenData.temperature = tempReading;
  }
  if (!isnan(humReading)) {
    gardenData.humidity = humReading;
  }

  int soilRaw = analogRead(SOIL_MOISTURE_PIN);
  gardenData.soilMoisture = map(soilRaw, 1023, 0, 0, 100);
  gardenData.soilMoisture = constrain(gardenData.soilMoisture, 0, 100);

  bool lightDigital = digitalRead(LIGHT_SENSOR_PIN);
  gardenData.lightLevel = lightDigital ? 0 : 1000;

  int waterRaw = analogRead(WATER_SENSOR_PIN);
  gardenData.rainDetected = (waterRaw > 500);

  gardenData.fireDetected = (gardenData.temperature > 45);

  if (rtcAvailable) {
    DateTime now = rtc.now();
    gardenData.currentTime = String(now.hour()) + ":" + 
                            (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":" + 
                            (now.second() < 10 ? "0" : "") + String(now.second());
  } else {
    gardenData.currentTime = String(millis() / 1000);
  }

  gardenData.lastUpdated = millis();
  gardenData.pumpRunning = relayDevices[8].isOn;

  Serial.println("[SENSORS] T:" + String(gardenData.temperature, 1) + "°C | " + 
                "H:" + String(gardenData.humidity, 1) + "% | " + 
                "S:" + String(gardenData.soilMoisture) + "% | " + 
                "L:" + String(gardenData.lightLevel) + " | " + 
                "R:" + String(gardenData.rainDetected ? "Y" : "N") + " | " +
                "F:" + String(gardenData.fireDetected ? "Y" : "N") + " | " +
                "Time:" + gardenData.currentTime);
}

void processGardenAutomation() {
  unsigned long currentTime = millis();
  
  updateTimeBasedStates();
  
  // Check user command override timeout
  if (userCommandActive && (currentTime - lastUserCommand > USER_COMMAND_OVERRIDE_TIME)) {
    userCommandActive = false;
  }
  
  // Process events by priority (5 levels now)
  for (int i = 0; i < 5; i++) {
    if (automationEvents[i].isActive && (currentTime - automationEvents[i].lastCheck >= automationEvents[i].interval)) {
      processEventByPriority((SystemPriority)i);
      automationEvents[i].lastCheck = currentTime;
    }
  }
}

void updateTimeBasedStates() {
  if (rtcAvailable) {
    DateTime now = rtc.now();
    int currentHour = now.hour();
    nightMode = (currentHour >= nightTimeHour || currentHour < dayTimeHour);
  }
}

void processEventByPriority(SystemPriority priority) {
  switch (priority) {
    case PRIORITY_USER_COMMAND:
      handleUserCommands();
      break;
    case PRIORITY_EMERGENCY:
      handleEmergencyEvents();
      break;
    case PRIORITY_CRITICAL:
      handleCriticalEvents();
      break;
    case PRIORITY_NORMAL:
      if (!userCommandActive) handleNormalAutomation();
      break;
    case PRIORITY_LOW:
      if (!userCommandActive) handleComfortAutomation();
      break;
  }
}

// ===== USER COMMAND HANDLING =====
void handleUserCommands() {
  // Process pending socket commands (handled in handleSocketHubMessage)
  // This maintains user command priority in the event system
}

void setUserCommandOverride(String reason) {
  userCommandActive = true;
  lastUserCommand = millis();
  sendSocketStatus("user_override", reason);
}

// ===== EMERGENCY EVENT HANDLING =====
void handleEmergencyEvents() {
  // Fire detection combo
  if (gardenData.fireDetected && !emergencyMode) {
    activateFireEmergency();
    return;
  }
  
  // Exit emergency if conditions clear
  if (!gardenData.fireDetected && emergencyMode) {
    deactivateEmergency();
  }
  
  // Flood protection
  if (gardenData.rainDetected && gardenData.pumpRunning) {
    stopPump("Emergency: Rain flood protection");
  }
}

void activateFireEmergency() {
  emergencyMode = true;
  
  // Emergency combo sequence (optimized timing)
  setRelayState(8, true, "Emergency: Fire suppression");    // Pump ON
  delay(100);
  setRelayState(1, true, "Emergency: Fire alarm");          // Buzzer ON
  delay(50);
  
  // Turn off all lighting for safety
  for (int i = 2; i <= 7; i++) {
    if (relayDevices[i].isOn) {
      setRelayState(i, false, "Emergency: Fire safety");
    }
    delay(30);
  }
  
  setRelayState(0, true, "Emergency: Ventilation");         // Fan ON
  setRGBColor(255, 0, 0); // Red alert
  
  sendSocketStatus("fire_emergency", "Fire protocol activated");
}

void deactivateEmergency() {
  emergencyMode = false;
  setRelayState(1, false, "Emergency cleared: Buzzer off");
  sendSocketStatus("emergency_cleared", "Emergency mode deactivated");
}

// ===== CRITICAL EVENT HANDLING =====
void handleCriticalEvents() {
  // Pump safety with temperature check
  if (gardenData.pumpRunning) {
    if (pumpStartTime > 0 && (millis() - pumpStartTime > maxPumpRunTime)) {
      stopPump("Critical: Safety timeout");
    } else if (gardenData.temperature > 40) {
      stopPump("Critical: Overheating protection");
    }
  }
  
  // Critical temperature management
  if (gardenData.temperature > 35 && !relayDevices[0].isOn) {
    setRelayState(0, true, "Critical: Temperature cooling");
  }
}

// ===== NORMAL AUTOMATION =====
void handleNormalAutomation() {
  if (!emergencyMode) {
    processIntelligentWatering();
  }
}

void processIntelligentWatering() {
  if (!autoWateringEnabled) return;
  
  bool shouldWater = false;
  String wateringReason = "";
  
  // Smart watering logic with multiple conditions
  if (gardenData.soilMoisture < soilMoistureThreshold && 
      !gardenData.rainDetected && 
      !gardenData.pumpRunning &&
      gardenData.humidity < humidityThreshold &&
      !nightMode) {
    
    shouldWater = true;
    wateringReason = "Smart: Low soil, good conditions";
  }
  
  // Stop watering conditions
  if (gardenData.pumpRunning && 
      (gardenData.soilMoisture > (soilMoistureThreshold + 15) ||
       gardenData.humidity > humidityThreshold ||
       nightMode ||
       gardenData.rainDetected)) {
    
    stopPump("Smart: Conditions changed");
    return;
  }
  
  if (shouldWater) {
    startPump(wateringReason);
  }
}

// ===== COMFORT AUTOMATION =====
void handleComfortAutomation() {
  if (!emergencyMode) {
    processIntelligentLighting();
    processIntelligentFan();
  }
}

void processIntelligentLighting() {
  if (!autoLightingEnabled) return;
  
  // Smart lighting: Dark + Not raining + Not emergency
  bool shouldLightBeOn = (gardenData.lightLevel < lightThreshold) && 
                        !gardenData.rainDetected && 
                        !emergencyMode;
  
  // Only control LED1 (relay index 2) for basic lighting
  if (shouldLightBeOn && !relayDevices[2].isOn) {
    setRelayState(2, true, "Smart: Intelligent lighting");
  } else if (!shouldLightBeOn && relayDevices[2].isOn) {
    setRelayState(2, false, "Smart: Light not needed");
  }
}

void processIntelligentFan() {
  if (!autoFanEnabled) return;
  
  // Smart fan control with temperature and humidity
  bool shouldFanBeOn = ((gardenData.temperature > 30) || 
                       (gardenData.humidity > 80)) && 
                       !emergencyMode;
  
  if (shouldFanBeOn && !relayDevices[0].isOn) {
    setRelayState(0, true, "Smart: Climate control");
  } else if (!shouldFanBeOn && relayDevices[0].isOn && 
             gardenData.temperature < 27 && gardenData.humidity < 70) {
    setRelayState(0, false, "Smart: Climate normal");
  }
}

void startPump(String reason) {
  setRelayState(8, true, reason);
  pumpStartTime = millis();
  sendSocketStatus("pump_start", reason);
}

void stopPump(String reason) {
  setRelayState(8, false, reason);
  pumpStartTime = 0;
  sendSocketStatus("pump_stop", reason);
}

void checkPumpSafety() {
  if (gardenData.pumpRunning && pumpStartTime > 0 && (millis() - pumpStartTime > maxPumpRunTime)) {
    stopPump("Safety timeout");
  }
}

// ===== DISPLAY FUNCTIONS =====
void updateDisplays() {
  systemStable = !anyHighPowerRelayActive();
  
  // Simple display rotation without conflicts
  static bool displayToggle = false;
  
  if (displayToggle && lcdAvailable) {
    updateLCDDisplay();
  } else if (!displayToggle && oledAvailable) {
    updateOLEDDisplay();
  }
  
  displayToggle = !displayToggle;
}

void updateLCDDisplay() {
  if (millis() - gardenData.lastPageChange > lcdPageDuration) {
    gardenData.lcdPage = (gardenData.lcdPage + 1) % LCD_PAGES;
    gardenData.lastPageChange = millis();
    lcd.clear();
  }

  switch (gardenData.lcdPage) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("T:");
      lcd.print(String(gardenData.temperature, 1));
      lcd.print("C H:");
      lcd.print(String(gardenData.humidity, 1));
      lcd.print("%");
      lcd.setCursor(0, 1);
      lcd.print("Time: ");
      lcd.print(gardenData.currentTime);
      break;
      
    case 1:
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
      break;
      
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Keypad:4x4 K:");
      lcd.print(String(localKeyCommands));
      lcd.setCursor(0, 1);
      lcd.print("Rain:");
      lcd.print(gardenData.rainDetected ? "Y" : "N");
      lcd.print(" Fire:");
      lcd.print(gardenData.fireDetected ? "Y" : "N");
      lcd.print(" RTC:");
      lcd.print(rtcAvailable ? "Y" : "N");
      break;
      
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("Socket:");
      lcd.print(socketHubConnected ? "Y" : "N");
      lcd.print(" Stable:");
      lcd.print(systemStable ? "Y" : "N");
      lcd.setCursor(0, 1);
      lcd.print("Keys:");
      lcd.print(String(localKeyCommands));
      lcd.print(" Up:");
      lcd.print(String((millis() - systemUptime) / 60000));
      lcd.print("m");
      break;
  }
}

void updateOLEDDisplay() {
  if (millis() - gardenData.lastOledUpdate > oledPageDuration) {
    gardenData.oledPage = (gardenData.oledPage + 1) % OLED_PAGES;
    gardenData.lastOledUpdate = millis();
    oled.clearDisplay();
  }

  switch (gardenData.oledPage) {
    case 0:
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("GARDEN SENSORS");
      oled.setCursor(0, 12);
      oled.print("Temp: ");
      oled.print(String(gardenData.temperature, 1));
      oled.print("C");
      oled.setCursor(0, 22);
      oled.print("Humidity: ");
      oled.print(String(gardenData.humidity, 1));
      oled.print("%");
      oled.setCursor(0, 32);
      oled.print("Soil: ");
      oled.print(String(gardenData.soilMoisture));
      oled.print("%");
      oled.setCursor(0, 42);
      oled.print("Light: ");
      oled.print(String(gardenData.lightLevel));
      oled.setCursor(0, 52);
      oled.print("Time: ");
      oled.print(gardenData.currentTime);
      break;
      
    case 1:
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("RELAY STATUS");
      for (int i = 0; i < 10; i++) {
        int x = (i % 5) * 25;
        int y = 15 + (i / 5) * 20;
        oled.drawRect(x, y, 20, 15, SSD1306_WHITE);
        if (relayDevices[i].isOn) {
          oled.fillRect(x + 1, y + 1, 18, 13, SSD1306_WHITE);
          oled.setTextColor(SSD1306_BLACK);
        } else {
          oled.setTextColor(SSD1306_WHITE);
        }
        oled.setCursor(x + 6, y + 4);
        oled.print(i < 9 ? String(i + 1) : "0");
        oled.setTextColor(SSD1306_WHITE);
      }
      oled.setCursor(0, 55);
      oled.print("Keys: ");
      oled.print(String(localKeyCommands));
      break;
      
    case 2:
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("4x4 KEYPAD");
      oled.setCursor(0, 12);
      oled.print("1-9,0=Relays");
      oled.setCursor(0, 22);
      oled.print("A=Water B=Light");
      oled.setCursor(0, 32);
      oled.print("C=Fan D=EmgStop");
      oled.setCursor(0, 42);
      oled.print("*=Status #=AllOff");
      oled.setCursor(0, 52);
      oled.print("Total: ");
      oled.print(String(localKeyCommands));
      break;
  }
  
  oled.display();
}

bool anyHighPowerRelayActive() {
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    if (relayDevices[i].powerSafeMode && relayDevices[i].isOn) {
      return true;
    }
  }
  return false;
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
  if (!socketHubConnected) return STATUS_OFFLINE;
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

// ===== RELAY CONTROL FUNCTIONS =====
void toggleRelay(int deviceIndex, String reason) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  relayDevices[deviceIndex].isOn = !relayDevices[deviceIndex].isOn;
  
  if (relayDevices[deviceIndex].activeHigh) {
    digitalWrite(relayDevices[deviceIndex].relayPin, relayDevices[deviceIndex].isOn ? HIGH : LOW);
  } else {
    digitalWrite(relayDevices[deviceIndex].relayPin, relayDevices[deviceIndex].isOn ? LOW : HIGH);
  }
  
  delay(20);
  
  relayDevices[deviceIndex].lastToggle = millis();
  relayDevices[deviceIndex].status = relayDevices[deviceIndex].isOn ? "on" : "off";
  
  relayCommands++;
  
  String triggerType = relayDevices[deviceIndex].activeHigh ? "HIGH" : "LOW";
  Serial.println("[RELAY] " + relayDevices[deviceIndex].deviceName + " TOGGLED to " + 
                String(relayDevices[deviceIndex].isOn ? "ON" : "OFF") + " - " + reason +
                " (Trigger: " + triggerType + ")");
  
  sendRelayDeviceStatus(deviceIndex);
}

void setRelayState(int deviceIndex, bool state, String reason) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  if (relayDevices[deviceIndex].isOn == state) return;
  
  relayDevices[deviceIndex].isOn = state;
  
  if (relayDevices[deviceIndex].activeHigh) {
    digitalWrite(relayDevices[deviceIndex].relayPin, state ? HIGH : LOW);
  } else {
    digitalWrite(relayDevices[deviceIndex].relayPin, state ? LOW : HIGH);
  }
  
  delay(20);
  
  relayDevices[deviceIndex].lastToggle = millis();
  relayDevices[deviceIndex].status = state ? "on" : "off";
  
  relayCommands++;
  
  String triggerType = relayDevices[deviceIndex].activeHigh ? "HIGH" : "LOW";
  Serial.println("[RELAY] " + relayDevices[deviceIndex].deviceName + " SET to " + 
                String(state ? "ON" : "OFF") + " - " + reason +
                " (Trigger: " + triggerType + ")");
  
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
    
  } else if (action == "STATUS") {
    success = true;
    result = relayDevices[deviceIndex].isOn ? "ON" : "OFF";
  }
  
  sendRelayResponse(serialNumber, action, success, result);
}

void sendRelayResponse(String serialNumber, String action, bool success, String result) {
  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"result\":\"" + result + "\",";
  json += "\"deviceId\":\"" + serialNumber + "\",";
  json += "\"command\":\"" + action + "\",";
  json += "\"type\":\"relay\",";
  json += "\"relay_type\":\"MIXED\",";
  json += "\"input_type\":\"4x4_KEYPAD\",";
  json += "\"power_safe\":true,";
  json += "\"timestamp\":" + String(millis());
  json += "}";

  Serial1.println("RESP:" + json);
}

void sendRelayDeviceStatus(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= TOTAL_RELAY_DEVICES) return;
  
  String statusMsg = "STS:RELAY:{";
  statusMsg += "\"deviceId\":\"" + relayDevices[deviceIndex].serialNumber + "\",";
  statusMsg += "\"name\":\"" + relayDevices[deviceIndex].deviceName + "\",";
  statusMsg += "\"state\":\"" + String(relayDevices[deviceIndex].isOn ? "on" : "off") + "\",";
  statusMsg += "\"pin\":" + String(relayDevices[deviceIndex].relayPin) + ",";
  statusMsg += "\"keypadKey\":\"" + String(relayDevices[deviceIndex].keypadKey) + "\",";
  statusMsg += "\"activeHigh\":" + String(relayDevices[deviceIndex].activeHigh ? "true" : "false") + ",";
  statusMsg += "\"localControl\":" + String(relayDevices[deviceIndex].localControl ? "true" : "false") + ",";
  statusMsg += "\"powerSafe\":" + String(relayDevices[deviceIndex].powerSafeMode ? "true" : "false") + ",";
  statusMsg += "\"inputType\":\"4x4_KEYPAD\",";
  statusMsg += "\"lastToggle\":" + String(relayDevices[deviceIndex].lastToggle) + ",";
  statusMsg += "\"lastKeyPress\":" + String(relayDevices[deviceIndex].lastKeyPress) + ",";
  statusMsg += "\"timestamp\":" + String(millis());
  statusMsg += "}";
  
  Serial1.println(statusMsg);
}

void sendRelayStatusToSocket() {
  Serial.println("[RELAY] Broadcasting status to socket...");
  
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    sendRelayDeviceStatus(i);
    delay(150);
  }
}

// ===== SOCKET HUB MESSAGE HANDLING =====
void handleSocketHubMessage(String message) {
  if (message.length() == 0) return;
  
  message.trim();
  
  socketHubConnected = true;
  lastSocketMessage = millis();

  if (message.startsWith("CMD:")) {
    String cmdData = message.substring(4);
    
    // User commands get highest priority
    setUserCommandOverride("Socket command: " + cmdData);
    
    if (cmdData.indexOf("GARDEN") >= 0) {
      handleGardenSocketCommand(message);
    } else if (cmdData.indexOf("RELAY") >= 0) {
      handleRelaySocketCommand(message);
    }
  }
}

void handleGardenSocketCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4);

  if (cmdData == "GARDEN:PUMP_ON") {
    startPump("User command via socket");
    sendSocketResponse("PUMP_ON", true, "Pump started by user");
  } else if (cmdData == "GARDEN:PUMP_OFF") {
    stopPump("User command via socket");
    sendSocketResponse("PUMP_OFF", true, "Pump stopped by user");
  } else if (cmdData == "GARDEN:AUTO_WATER_TOGGLE") {
    autoWateringEnabled = !autoWateringEnabled;
    sendSocketResponse("AUTO_WATER", true, "Auto watering: " + String(autoWateringEnabled ? "ON" : "OFF"));
  } else if (cmdData == "GARDEN:AUTO_LIGHT_TOGGLE") {
    autoLightingEnabled = !autoLightingEnabled;
    sendSocketResponse("AUTO_LIGHT", true, "Auto lighting: " + String(autoLightingEnabled ? "ON" : "OFF"));
  } else if (cmdData == "GARDEN:AUTO_FAN_TOGGLE") {
    autoFanEnabled = !autoFanEnabled;
    sendSocketResponse("AUTO_FAN", true, "Auto fan: " + String(autoFanEnabled ? "ON" : "OFF"));
  } else if (cmdData.startsWith("GARDEN:RGB_")) {
    handleRGBCommand(cmdData);
  } else if (cmdData.startsWith("GARDEN:SET_")) {
    handleThresholdCommand(cmdData);
  }
}

void handleRelaySocketCommand(String cmdMessage) {
  String cmdData = cmdMessage.substring(4);
  
  int colonIndex = cmdData.indexOf(':');
  if (colonIndex <= 0) {
    return;
  }
  
  String serialNumber = cmdData.substring(0, colonIndex);
  String action = cmdData.substring(colonIndex + 1);
  
  if (serialNumber.length() != 32 || !serialNumber.startsWith("RELAY")) {
    sendRelayResponse(serialNumber, action, false, "Invalid serial format");
    return;
  }
  
  // User relay commands get priority
  setUserCommandOverride("Relay command: " + serialNumber + ":" + action);
  
  handleRelayCommand(serialNumber, action);
}

// ===== SOCKET COMMUNICATION FUNCTIONS =====
void sendSocketResponse(String command, bool success, String message) {
  String response = "RESP:{";
  response += "\"success\":" + String(success ? "true" : "false") + ",";
  response += "\"command\":\"" + command + "\",";
  response += "\"message\":\"" + message + "\",";
  response += "\"type\":\"garden\",";
  response += "\"user_override\":" + String(userCommandActive ? "true" : "false") + ",";
  response += "\"timestamp\":" + String(millis());
  response += "}";
  
  Serial1.println(response);
}

void sendSocketStatus(String statusType, String data) {
  String statusMsg = "STS:GARDEN:{";
  statusMsg += "\"status_type\":\"" + statusType + "\",";
  statusMsg += "\"data\":\"" + data + "\",";
  statusMsg += "\"user_override\":" + String(userCommandActive ? "true" : "false") + ",";
  statusMsg += "\"emergency_mode\":" + String(emergencyMode ? "true" : "false") + ",";
  statusMsg += "\"timestamp\":" + String(millis());
  statusMsg += "}";
  
  Serial1.println(statusMsg);
}

void sendGardenData() {
  String gardenDataMsg = "GARDEN_DATA:{";
  gardenDataMsg += "\"temperature\":" + String(gardenData.temperature, 1) + ",";
  gardenDataMsg += "\"humidity\":" + String(gardenData.humidity, 1) + ",";
  gardenDataMsg += "\"soil_moisture\":" + String(gardenData.soilMoisture) + ",";
  gardenDataMsg += "\"light_level\":" + String(gardenData.lightLevel) + ",";
  gardenDataMsg += "\"rain_detected\":" + String(gardenData.rainDetected ? "true" : "false") + ",";
  gardenDataMsg += "\"fire_detected\":" + String(gardenData.fireDetected ? "true" : "false") + ",";
  gardenDataMsg += "\"pump_running\":" + String(gardenData.pumpRunning ? "true" : "false") + ",";
  gardenDataMsg += "\"user_override\":" + String(userCommandActive ? "true" : "false") + ",";
  gardenDataMsg += "\"emergency_mode\":" + String(emergencyMode ? "true" : "false") + ",";
  gardenDataMsg += "\"system_stable\":" + String(systemStable ? "true" : "false") + ",";
  gardenDataMsg += "\"current_time\":\"" + gardenData.currentTime + "\",";
  gardenDataMsg += "\"rgb_status\":\"" + gardenData.rgbStatus + "\",";
  gardenDataMsg += "\"timestamp\":" + String(millis());
  gardenDataMsg += "}";
  
  Serial1.println(gardenDataMsg);
}

void handleRGBCommand(String cmdData) {
  if (cmdData == "GARDEN:RGB_TEST") {
    testRGBSequence();
    sendSocketResponse("RGB_TEST", true, "RGB test sequence completed");
  } else if (cmdData == "GARDEN:RGB_AUTO") {
    // Resume automatic RGB control
    sendSocketResponse("RGB_AUTO", true, "RGB auto mode enabled");
  } else if (cmdData.startsWith("GARDEN:RGB_MANUAL:")) {
    String colorData = cmdData.substring(18);
    setManualRGBColor(colorData);
  }
}

void handleThresholdCommand(String cmdData) {
  if (cmdData.startsWith("GARDEN:SET_SOIL_THRESHOLD:")) {
    int value = cmdData.substring(26).toInt();
    soilMoistureThreshold = constrain(value, 0, 100);
    sendSocketResponse("SET_SOIL_THRESHOLD", true, "Soil threshold set to " + String(value) + "%");
  } else if (cmdData.startsWith("GARDEN:SET_LIGHT_THRESHOLD:")) {
    int value = cmdData.substring(27).toInt();
    lightThreshold = constrain(value, 0, 1000);
    sendSocketResponse("SET_LIGHT_THRESHOLD", true, "Light threshold set to " + String(value));
  }
}

void testRGBSequence() {
  setRGBColor(255, 0, 0); delay(500);
  setRGBColor(0, 255, 0); delay(500);
  setRGBColor(0, 0, 255); delay(500);
  setRGBColor(255, 255, 255); delay(500);
  setRGBColor(0, 0, 0);
}

void setManualRGBColor(String colorData) {
  int comma1 = colorData.indexOf(',');
  int comma2 = colorData.indexOf(',', comma1 + 1);
  
  if (comma1 > 0 && comma2 > comma1) {
    int red = colorData.substring(0, comma1).toInt();
    int green = colorData.substring(comma1 + 1, comma2).toInt();
    int blue = colorData.substring(comma2 + 1).toInt();
    
    red = constrain(red, 0, 255);
    green = constrain(green, 0, 255);
    blue = constrain(blue, 0, 255);
    
    setRGBColor(red, green, blue);
    sendSocketResponse("RGB_MANUAL", true, "RGB set to " + String(red) + "," + String(green) + "," + String(blue));
  } else {
    sendSocketResponse("RGB_MANUAL", false, "Invalid color format");
  }
}

void checkSystemHealth() {
  if (socketHubConnected && (millis() - lastSocketMessage > 120000)) {
    socketHubConnected = false;
    sendSocketStatus("disconnected", "Socket hub timeout");
  }
  
  if (!anyHighPowerRelayActive()) {
    systemStable = true;
  }
}

void sendSystemHeartbeat() {
  // Simple heartbeat without debug output
  sendSocketStatus("heartbeat", "Garden hub running");
}

void printSystemStatus() {
  Serial.println("\n======= MEGA GARDEN HUB STATUS (4x4 KEYPAD) =======");
  Serial.println("Hub ID: " + String(HUB_ID));
  Serial.println("Version: " + String(FIRMWARE_VERSION));
  Serial.println("Input Method: 4x4 Matrix Keypad");
  Serial.println("Uptime: " + String((millis() - systemUptime) / 1000) + " seconds");
  Serial.println("Key Commands: " + String(localKeyCommands));
  Serial.println("System Stable: " + String(systemStable ? "YES" : "NO"));

  Serial.println("\n--- 4x4 Keypad Configuration ---");
  Serial.println("Keypad Type: 4x4 Matrix (16 keys)");
  Serial.print("Row Pins: ");
  for (int i = 0; i < KEYPAD_ROWS; i++) {
    Serial.print(String(keypadRowPins[i]) + " ");
  }
  Serial.println();
  Serial.print("Col Pins: ");
  for (int i = 0; i < KEYPAD_COLS; i++) {
    Serial.print(String(keypadColPins[i]) + " ");
  }
  Serial.println();
  
  Serial.println("\n--- Key Mappings ---");
  Serial.println("Relay Control: 1-9,0 (keys 1-9 = relays 1-9, key 0 = relay 10)");
  Serial.println("A = Auto Water Toggle");
  Serial.println("B = Auto Light Toggle"); 
  Serial.println("C = Auto Fan Toggle");
  Serial.println("D = Emergency Stop");
  Serial.println("* = System Status");
  Serial.println("# = All Relays Off");

  Serial.println("\n--- I2C Device Status ---");
  Serial.println("RTC DS3231: " + String(rtcAvailable ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("LCD 16x2: " + String(lcdAvailable ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("OLED 0.96\": " + String(oledAvailable ? "CONNECTED" : "DISCONNECTED"));

  Serial.println("\n--- Socket Hub Connection ---");
  Serial.println("Socket Hub: " + String(socketHubConnected ? "CONNECTED" : "DISCONNECTED"));

  Serial.println("\n--- Garden System ---");
  Serial.println("Temperature: " + String(gardenData.temperature, 1) + "°C");
  Serial.println("Humidity: " + String(gardenData.humidity, 1) + "%");
  Serial.println("Soil Moisture: " + String(gardenData.soilMoisture) + "%");
  Serial.println("Light Level: " + String(gardenData.lightLevel));
  Serial.println("Rain: " + String(gardenData.rainDetected ? "DETECTED" : "NONE"));
  Serial.println("Fire: " + String(gardenData.fireDetected ? "DETECTED" : "NONE"));
  Serial.println("Pump: " + String(gardenData.pumpRunning ? "RUNNING" : "STOPPED"));
  Serial.println("Current Time: " + gardenData.currentTime);

  Serial.println("\n--- 10 Relay Status ---");
  for (int i = 0; i < TOTAL_RELAY_DEVICES; i++) {
    String triggerType = relayDevices[i].activeHigh ? "HIGH" : "LOW";
    String powerMode = relayDevices[i].powerSafeMode ? "SAFE" : "NORMAL";
    Serial.println("Relay " + String(i + 1) + " (" + relayDevices[i].deviceName + "): " + 
                   String(relayDevices[i].isOn ? "ON" : "OFF") + 
                   " | Pin " + String(relayDevices[i].relayPin) + 
                   " | Key '" + String(relayDevices[i].keypadKey) + 
                   "' | Trigger " + triggerType + 
                   " | Power " + powerMode);
  }

  Serial.println("\n--- Keypad Control System ---");
  Serial.println("Input Type: 4x4 Matrix Keypad");
  Serial.println("Total Keys: 16");
  Serial.println("Total Key Commands: " + String(localKeyCommands));
  Serial.println("Debounce Time: " + String(KEY_DEBOUNCE_TIME) + "ms");

  Serial.println("\n--- Automation Settings ---");
  Serial.println("Auto Watering: " + String(autoWateringEnabled ? "ENABLED" : "DISABLED"));
  Serial.println("Auto Lighting: " + String(autoLightingEnabled ? "ENABLED" : "DISABLED"));
  Serial.println("Auto Fan: " + String(autoFanEnabled ? "ENABLED" : "DISABLED"));

  Serial.println("==========================================================\n");
}

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)&__brkval);
}