// UNO Receive - NON-BLOCKING pulse counting
#define ESP01_PIN_START 2
#define NUM_DOORS 8
#define COMMAND_COOLDOWN 2000

// Global variables
bool lastStates[NUM_DOORS] = {false};
unsigned long lastCheck = 0;
unsigned long lastCommandTime[NUM_DOORS] = {0};

// NON-BLOCKING pulse counting structure
struct PulseCounter {
  int pin;
  int count;
  unsigned long startTime;
  bool active;
  bool waitingForHigh;
};

PulseCounter pulseCounters[NUM_DOORS];

// Function declarations
void checkDoorSignals();
void updatePulseCounters();

void setup() {
  Serial.begin(115200);
  
  for (int i = 0; i < NUM_DOORS; i++) {
    pinMode(ESP01_PIN_START + i, INPUT_PULLUP);
    lastStates[i] = true;
    lastCommandTime[i] = 0;
    
    // Initialize pulse counters
    pulseCounters[i].pin = ESP01_PIN_START + i;
    pulseCounters[i].count = 0;
    pulseCounters[i].active = false;
    pulseCounters[i].waitingForHigh = false;
  }
  
  Serial.println("UNO Receive Ready - NON-BLOCKING");
}

void loop() {
  // Check signals every 10ms for responsiveness
  if (millis() - lastCheck > 10) {
    checkDoorSignals();
    updatePulseCounters();
    lastCheck = millis();
  }
}

void checkDoorSignals() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < NUM_DOORS; i++) {
    bool currentState = digitalRead(ESP01_PIN_START + i);
    
    // Detect falling edge and start pulse counting
    if (currentState == LOW && lastStates[i] == HIGH) {
      
      if (currentTime - lastCommandTime[i] < COMMAND_COOLDOWN) {
        lastStates[i] = currentState;
        continue;
      }
      
      // START NON-BLOCKING pulse counting
      if (!pulseCounters[i].active) {
        pulseCounters[i].active = true;
        pulseCounters[i].count = 1;  // First pulse detected
        pulseCounters[i].startTime = currentTime;
        pulseCounters[i].waitingForHigh = true;
        
        Serial.println("Door " + String(i+1) + " pulse counting started");
      }
    }
    
    lastStates[i] = currentState;
  }
}

void updatePulseCounters() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < NUM_DOORS; i++) {
    if (!pulseCounters[i].active) continue;
    
    bool pinState = digitalRead(pulseCounters[i].pin);
    
    // Timeout check
    if (currentTime - pulseCounters[i].startTime > 2000) {
      // TIMEOUT - send command
      if (pulseCounters[i].count >= 1 && pulseCounters[i].count <= 7) {
        Serial.println("SERVO" + String(pulseCounters[i].count));
        lastCommandTime[i] = currentTime;
        
        Serial.println("Door " + String(i+1) + " -> SERVO" + String(pulseCounters[i].count));
      }
      
      pulseCounters[i].active = false;
      continue;
    }
    
    // State machine for pulse counting
    if (pulseCounters[i].waitingForHigh) {
      if (pinState == HIGH) {
        pulseCounters[i].waitingForHigh = false;
      }
    } else {
      if (pinState == LOW) {
        pulseCounters[i].count++;
        pulseCounters[i].waitingForHigh = true;
      }
    }
  }
}