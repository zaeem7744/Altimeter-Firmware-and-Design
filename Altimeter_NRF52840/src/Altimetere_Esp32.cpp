#include <SPIFFS.h>
#include <BluetoothSerial.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to enable it
#endif

BluetoothSerial SerialBT;

#define LDR_PIN 15
#define BUTTON_PIN 4
#define LED_PIN 2  // Built-in LED (usually GPIO 2)

const unsigned long sampleInterval = 200; // ms
unsigned long lastSample = 0;
unsigned long lastButtonPress = 0;
unsigned long lastBlink = 0;
const unsigned long debounceDelay = 250;
const unsigned long triplePressTimeout = 1000;

// LED blinking patterns
const unsigned long btBlinkInterval = 1000; // 1 second for Bluetooth waiting
const unsigned long btConnectedBlinkInterval = 2000; // 2 seconds for Bluetooth connected
const unsigned long buttonBlinkDuration = 100; // 100ms for button feedback

int buttonPressCount = 0;
bool logging = false; // Start in idle mode
bool btActive = false;
bool ledState = false;
bool buttonBlinkActive = false;
unsigned long buttonBlinkStart = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED off

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  Serial.println("ESP32 LDR Logger started - IDLE mode");
  Serial.println("Press button once to start logging");
}

void loop() {
  // Sample LDR and store if logging is enabled
  if (logging && !btActive && (millis() - lastSample >= sampleInterval)) {
    int value = analogRead(LDR_PIN);
    appendToFile(value);
    Serial.printf("Logged: %d\n", value);
    lastSample = millis();
  }

  // Button press detection with debouncing
  handleButtonPress();
  
  // Handle Bluetooth communication if active
  if (btActive) {
    if (SerialBT.hasClient()) {
      handleBluetoothCommands();
    }
    // Update Bluetooth LED status
    handleBluetoothLED();
  }
  
  // Handle button feedback LED
  handleButtonFeedbackLED();
  
  // Handle idle state LED (slow blink when ready but not doing anything)
  handleIdleLED();
}

void handleButtonPress() {
  int buttonState = digitalRead(BUTTON_PIN);
  
  if (buttonState == LOW && (millis() - lastButtonPress > debounceDelay)) {
    lastButtonPress = millis();
    buttonPressCount++;
    
    // Button press feedback - quick blink
    buttonBlinkActive = true;
    buttonBlinkStart = millis();
    digitalWrite(LED_PIN, HIGH);
    
    Serial.printf("Button press #%d detected\n", buttonPressCount);
    
    // Start counting for triple press
    unsigned long pressStartTime = millis();
    
    // Wait for additional presses or timeout
    while (millis() - pressStartTime < triplePressTimeout) {
      if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastButtonPress > debounceDelay)) {
        lastButtonPress = millis();
        buttonPressCount++;
        
        // Additional button press feedback
        digitalWrite(LED_PIN, HIGH);
        delay(50);
        digitalWrite(LED_PIN, LOW);
        delay(50);
        digitalWrite(LED_PIN, HIGH);
        
        Serial.printf("Button press #%d detected\n", buttonPressCount);
      }
      delay(50);
    }
    
    // Evaluate button press pattern
    if (buttonPressCount == 1) {
      // Single press - start logging
      if (!logging && !btActive) {
        logging = true;
        Serial.println("STARTED logging data");
        // Logging feedback - 2 quick blinks
        for(int i = 0; i < 2; i++) {
          digitalWrite(LED_PIN, HIGH);
          delay(100);
          digitalWrite(LED_PIN, LOW);
          delay(100);
        }
      }
    } else if (buttonPressCount == 2) {
      // Double press - stop logging
      if (logging && !btActive) {
        logging = false;
        Serial.println("STOPPED logging data");
        // Stop logging feedback - 3 quick blinks
        for(int i = 0; i < 3; i++) {
          digitalWrite(LED_PIN, HIGH);
          delay(100);
          digitalWrite(LED_PIN, LOW);
          delay(100);
        }
      }
    } else if (buttonPressCount >= 3) {
      // Triple press - start Bluetooth
      if (!btActive) {
        logging = false;
        startBluetooth();
        // Bluetooth start feedback - long blink
        digitalWrite(LED_PIN, HIGH);
        delay(500);
        digitalWrite(LED_PIN, LOW);
      }
    }
    
    buttonPressCount = 0;
  }
}

void handleBluetoothLED() {
  if (!btActive) return;
  
  unsigned long currentTime = millis();
  
  if (SerialBT.hasClient()) {
    // Bluetooth connected - slow blink (2 seconds)
    if (currentTime - lastBlink >= btConnectedBlinkInterval) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastBlink = currentTime;
    }
  } else {
    // Bluetooth waiting for connection - faster blink (1 second)
    if (currentTime - lastBlink >= btBlinkInterval) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastBlink = currentTime;
    }
  }
}

void handleButtonFeedbackLED() {
  if (buttonBlinkActive && (millis() - buttonBlinkStart >= buttonBlinkDuration)) {
    digitalWrite(LED_PIN, LOW);
    buttonBlinkActive = false;
  }
}

void handleIdleLED() {
  // Only handle idle LED if not in Bluetooth mode and no button feedback active
  if (!btActive && !buttonBlinkActive && !logging) {
    unsigned long currentTime = millis();
    
    // Very slow blink when idle (4 seconds) - system is ready
    if (currentTime - lastBlink >= 4000) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastBlink = currentTime;
    }
  }
}

void appendToFile(int value) {
  File file = SPIFFS.open("/data.txt", FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  file.println(value);
  file.close();
}

void startBluetooth() {
  Serial.println("Starting Bluetooth...");
  SerialBT.begin("ESP32_LDR_LOGGER");
  btActive = true;
  lastBlink = millis(); // Reset blink timer
  
  Serial.println("Bluetooth started. Waiting for connection...");
  Serial.println("Device name: ESP32_LDR_LOGGER");
}

void handleBluetoothCommands() {
  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n');
    command.trim();
    
    // Command received feedback - quick blink
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    
    Serial.printf("Received command: %s\n", command.c_str());
    
    if (command == "STATS") {
      sendMemoryStats();
    } else if (command == "GET_DATA") {
      sendStoredData();
    } else if (command == "ERASE") {
      eraseData();
    } else if (command == "DISCONNECT") {
      SerialBT.println("DISCONNECT_ACK");
      SerialBT.flush(); // ← ADD THIS LINE
      delay(100);
      SerialBT.disconnect();
      btActive = false;
      logging = false;
      digitalWrite(LED_PIN, LOW);
      Serial.println("Bluetooth disconnected");
    }
  }
}

void sendMemoryStats() {
  int lineCount = 0;
  
  // Check if file exists and count lines
  if (SPIFFS.exists("/data.txt")) {
    File file = SPIFFS.open("/data.txt", FILE_READ);
    if (file) {
      while (file.available()) {
        file.readStringUntil('\n');
        lineCount++;
      }
      file.close();
    }
  }
  
  // Get SPIFFS info
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  float usedPercent = (float)usedBytes / totalBytes * 100;
  
  String stats = "STATS:Samples=" + String(lineCount) + 
                 ",Used=" + String(usedBytes) + "B" +
                 ",Total=" + String(totalBytes) + "B" +
                 ",Usage=" + String(usedPercent, 1) + "%";
  
  SerialBT.println(stats);
  SerialBT.flush(); // ← ADD THIS LINE - Force immediate send
  Serial.println("Sent stats: " + stats);
}


void sendStoredData() {
  if (!SPIFFS.exists("/data.txt")) {
    SerialBT.println("ERROR: No data file exists");
    SerialBT.println("DATA_END");
    SerialBT.flush(); // ← ADD THIS LINE
    return;
  }
  
  File file = SPIFFS.open("/data.txt", FILE_READ);
  if (!file) {
    SerialBT.println("ERROR: Cannot open data file");
    SerialBT.println("DATA_END");
    SerialBT.flush(); // ← ADD THIS LINE
    return;
  }
  
  SerialBT.println("DATA_START");
  SerialBT.flush(); // ← ADD THIS LINE
  delay(100);
  
  int sentCount = 0;
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      SerialBT.println(line);
      sentCount++;
      
      // Send data in chunks with flushing
      if (sentCount % 10 == 0) {
        SerialBT.flush(); // ← FLUSH EVERY 10 SAMPLES
        delay(5);
      }
    }
  }
  file.close();
  
  SerialBT.println("DATA_END");
  SerialBT.flush(); // ← ADD THIS LINE
  Serial.printf("Data sent via Bluetooth: %d samples\n", sentCount);
}


void eraseData() {
  bool fileExisted = SPIFFS.exists("/data.txt");
  
  if (fileExisted) {
    if (SPIFFS.remove("/data.txt")) {
      SerialBT.println("ERASE_SUCCESS: All data erased");
      SerialBT.flush(); // ← ADD THIS LINE
      Serial.println("Data file erased");
    } else {
      SerialBT.println("ERASE_ERROR: Failed to erase data");
      SerialBT.flush(); // ← ADD THIS LINE
      return;
    }
  } else {
    SerialBT.println("ERASE_SUCCESS: No data to erase");
    SerialBT.flush(); // ← ADD THIS LINE
  }
  
  // Verify erase by checking file existence
  if (!SPIFFS.exists("/data.txt")) {
    SerialBT.println("VERIFY: Data file successfully removed");
    SerialBT.flush(); // ← ADD THIS LINE
  } else {
    SerialBT.println("WARNING: Data file still exists after erase");
    SerialBT.flush(); // ← ADD THIS LINE
  }
}