// UNO Receive - Monitors ESP-01 signals and sends serial commands
#define ESP01_PIN_START 2  // Pins 2-8 for ESP-01 Door 1-7
#define NUM_DOORS 7

bool lastStates[NUM_DOORS] = {false};
unsigned long lastCheck = 0;

void setup() {
  Serial.begin(115200);
  
  // Setup pins 2-8 for ESP-01 signals with pulldown
  for (int i = 0; i < NUM_DOORS; i++) {
    pinMode(ESP01_PIN_START + i, INPUT_PULLUP);  // Use pullup, ESP-01 will pull LOW when active
    lastStates[i] = true;  // Start HIGH due to pullup
  }
  
  Serial.println("UNO Receive Ready - Monitoring " + String(NUM_DOORS) + " doors");
}

void loop() {
  // Check each ESP-01 signal pin every 50ms
  if (millis() - lastCheck > 50) {
    checkDoorSignals();
    lastCheck = millis();
  }
}

void checkDoorSignals() {
  for (int i = 0; i < NUM_DOORS; i++) {
    bool currentState = digitalRead(ESP01_PIN_START + i);
    
    // Detect falling edge (HIGH to LOW) since we use pullup
    if (currentState == LOW && lastStates[i] == HIGH) {
      // Count pulses from this ESP-01
      int pulses = countPulses(ESP01_PIN_START + i);
      
      // Only send if pulse count is reasonable (1-7)
      if (pulses >= 1 && pulses <= 7) {
        Serial.println("SERVO" + String(pulses));
        
        // Debug output (comment out in production)
        // Serial.println("Door detected: Pin " + String(ESP01_PIN_START + i) + 
        //                " Pulses: " + String(pulses));
      }
      
      delay(500);  // Longer debounce to prevent duplicate commands
    }
    
    lastStates[i] = currentState;
  }
}

int countPulses(int pin) {
  int count = 1;  // Already detected first LOW pulse
  delay(250);     // Shorter wait for additional pulses
  
  unsigned long startTime = millis();
  while (millis() - startTime < 1500) {  // Reduced timeout
    // Wait for pin to go HIGH (end of pulse)
    while (digitalRead(pin) == LOW && (millis() - startTime < 1500)) delay(5);
    
    // Short delay to avoid noise
    delay(50);
    
    // Wait for pin to go LOW again (next pulse)
    while (digitalRead(pin) == HIGH && (millis() - startTime < 1500)) delay(5);
    
    // If LOW again, increment count
    if (digitalRead(pin) == LOW && (millis() - startTime < 1500)) {
      count++;
      delay(50);  // Small delay between pulse counts
    } else {
      break;  // No more pulses
    }
  }
  
  return count;
}