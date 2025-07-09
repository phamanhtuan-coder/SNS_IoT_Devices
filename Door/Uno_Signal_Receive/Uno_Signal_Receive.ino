// UNO Signal Receiver - Optimized Version
#define ESP01_PIN_START 2  // Pins 2-9 for 8 doors
#define NUM_DOORS 8

// Global variables
bool currentPinStates[NUM_DOORS] = {LOW};
bool lastPinStates[NUM_DOORS] = {LOW};
unsigned long lastCommandTime[NUM_DOORS] = {0};
unsigned long lastStateChange[NUM_DOORS] = {0};
unsigned long lastCheck = 0;
unsigned long lastDebugPrint = 0;
const unsigned long debounceDelay = 50; // Debounce time in ms
const unsigned long commandCooldown = 1000; // Min time between commands
const unsigned long debugInterval = 10000; // Print debug every 10 seconds (reduced)

// Non-blocking serial communication
const int MAX_BUFFER = 32;
char serialBuffer[MAX_BUFFER];
int bufferIndex = 0;
unsigned long lastSerialActivity = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize all pins as inputs with PULL-UP
  for (int i = 0; i < NUM_DOORS; i++) {
    pinMode(ESP01_PIN_START + i, INPUT_PULLUP);
    lastPinStates[i] = HIGH; // Start HIGH due to pull-up
  }
  
  Serial.println("UNO_RX:READY");
  Serial.println("DirectCmd:8_DOORS:PINS_2-9:PULLUP");
  Serial.println("DEBUG:REDUCED_OUTPUT_MODE");
  
  delay(100);
  printPinStates("INIT");
}

void loop() {
  // Read pin states regularly
  if (millis() - lastCheck > 10) {
    checkDoorSignals();
    lastCheck = millis();
  }
  
  // Reduced debug output - only every 10 seconds
  if (millis() - lastDebugPrint > debugInterval) {
    printPinStates("STATUS");
    lastDebugPrint = millis();
  }
  
  // Non-blocking serial reading
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    lastSerialActivity = millis();
    
    if (inChar == '\n') {
      serialBuffer[bufferIndex] = '\0';
      processSerialCommand(String(serialBuffer));
      bufferIndex = 0;
    } else if (bufferIndex < MAX_BUFFER - 1) {
      serialBuffer[bufferIndex++] = inChar;
    }
  }
  
  // Serial timeout
  if (bufferIndex > 0 && millis() - lastSerialActivity > 1000) {
    bufferIndex = 0;
  }
}

void checkDoorSignals() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < NUM_DOORS; i++) {
    // Read current state
    bool newState = digitalRead(ESP01_PIN_START + i);
    
    // Check for state change
    if (newState != currentPinStates[i]) {
      lastStateChange[i] = currentTime;
      currentPinStates[i] = newState;
    }
    
    // If state is stable after debounce time
    if ((currentTime - lastStateChange[i]) > debounceDelay) {
      if (currentPinStates[i] != lastPinStates[i] && 
          (currentTime - lastCommandTime[i]) > commandCooldown) {
        
        lastPinStates[i] = currentPinStates[i];
        int doorNumber = i + 1;
        int state = (currentPinStates[i] == LOW) ? 1 : 0;
        
        // Send command to Servo Controller
        Serial.println("D" + String(doorNumber) + ":" + String(state));
        
        // Minimal logging - only state changes
        Serial.println("DOOR" + String(doorNumber) + ":" + 
                      (state == 1 ? "OPEN" : "CLOSE"));
        
        lastCommandTime[i] = currentTime;
      }
    }
  }
}

void printPinStates(String label) {
  Serial.print(label + ":");
  for (int i = 0; i < NUM_DOORS; i++) {
    bool pinState = digitalRead(ESP01_PIN_START + i);
    Serial.print("D" + String(i + 1) + ":");
    Serial.print(pinState ? "H" : "L");
    if (i < NUM_DOORS - 1) Serial.print(",");
  }
  Serial.println();
}

void processSerialCommand(String cmd) {
  cmd.trim();
  
  // Ignore servo controller messages
  if (cmd.startsWith("ACK:") || 
      cmd.startsWith("UNO_SERVO:") || 
      cmd.startsWith("DirectCmd:") || 
      cmd.startsWith("RESET:") ||
      cmd == "") {
    return;
  }
  
  if (cmd == "TEST") {
    runTestSequence();
  }
  else if (cmd == "DEBUG") {
    printPinStates("MANUAL");
  }
  else if (cmd == "PINS") {
    printDetailedPinInfo();
  }
}

void printDetailedPinInfo() {
  Serial.println("PIN_INFO:");
  for (int i = 0; i < NUM_DOORS; i++) {
    bool pinState = digitalRead(ESP01_PIN_START + i);
    Serial.println("D" + String(i + 1) + ":PIN" + String(ESP01_PIN_START + i) + 
                  ":" + String(pinState ? "H" : "L"));
  }
}

void runTestSequence() {
  Serial.println("TEST:START");
  for (int i = 1; i <= NUM_DOORS; i++) {
    Serial.println("D" + String(i) + ":1");
    delay(1000);
    Serial.println("D" + String(i) + ":0");
    delay(1000);
  }
  Serial.println("TEST:END");
}