// main.cpp - SENSOR-INTEGRATED FLIGHT LOGGER
#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Wire.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_BMP3XX.h>
#include "flash_storage.h"
void setLEDColor(uint8_t red, uint8_t green, uint8_t blue);

// ==================== CONFIGURATION ====================
#define BUTTON_PIN D5

// External sensor I2C addresses
#define LSM6DSO32_ADDR 0x6A
#define BMP3XX_ADDR    0x77

// Sea level pressure for altitude calculation (adjust for launch site)
#define SEALEVELPRESSURE_HPA (1013.25)

#define DEVICE_NAME "RocketTelemetry"
#define SERVICE_UUID "19B10000-E8F2-537E-4F6C-D104768A1214"
#define DATA_UUID "19B10001-E8F2-537E-4F6C-D104768A1214"
#define COMMAND_UUID "19B10002-E8F2-537E-4F6C-D104768A1214"

#define MAIN_LOOP_DELAY 10
#define DEBOUNCE_DELAY 50
#define MULTI_PRESS_WINDOW 500
#define LONG_PRESS_THRESHOLD 10000
#define BT_DISCONNECT_DURATION 2000

// ==================== COMMANDS ====================
#define CMD_STATUS "STATUS"
#define CMD_MEMORY_STATUS "MEMORY_STATUS"
#define CMD_EXTRACT_DATA "EXTRACT_DATA"
#define CMD_CLEAR_MEMORY "CLEAR_MEMORY"
// Serial-only debug command to dump flash contents as CSV
#define CMD_DUMP_SERIAL "DUMP"

// ==================== SYSTEM STATES ====================
enum DeviceMode {
  MODE_IDLE,
  MODE_LOGGING,
  MODE_BT_ADVERTISING,
  MODE_BT_CONNECTED,
  MODE_BT_DISCONNECTING,
  MODE_MEMORY_CLEAR
};

// ==================== STATE MANAGER ====================
struct SystemState {
  DeviceMode mode;
  bool logging;
  bool bleActive;
  bool bleConnected;
  unsigned long modeStartTime;
  unsigned long disconnectStartTime;
  unsigned long lastTelemetryTime;
  unsigned int dataPoints;
  String lastCommand;
  bool pendingDisconnect;
};

class StateManager {
private:
  SystemState state;
  
  void initializeState() {
    state.mode = MODE_IDLE;
    state.logging = false;
    state.bleActive = false;
    state.bleConnected = false;
    state.modeStartTime = millis();
    state.disconnectStartTime = 0;
    state.lastTelemetryTime = 0;
    state.dataPoints = 0;
    state.lastCommand = "";
    state.pendingDisconnect = false;
  }

public:
  StateManager() {
    initializeState();
  }
  
  void begin() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    setLEDColor(0, 0, 0);
    
    analogWrite(LED_BLUE, 200);
    delay(500);
    setLEDColor(0, 0, 0);
    delay(200);
    
    Serial.begin(115200);
    // Wait for serial with timeout for independent operation
    unsigned long serialTimeout = millis() + 2000; // 2 second timeout
    while (!Serial && millis() < serialTimeout) {
      delay(10);
    }
    delay(100);
    
    // Only print if serial is available
    if (Serial) {
      Serial.println("🚀 State Manager Initialized");
      Serial.println("📡 BLE Device: " + String(DEVICE_NAME));
      Serial.println("💾 Flash Storage: Ready");
    }
  }
  
  void setMode(DeviceMode newMode) {
    state.mode = newMode;
    state.modeStartTime = millis();
    
    if (Serial) {
      switch(newMode) {
        case MODE_IDLE:
          Serial.println("🔵 Mode: IDLE");
          break;
        case MODE_LOGGING:
          Serial.println("🟢 Mode: LOGGING");
          break;
        case MODE_BT_ADVERTISING:
          Serial.println("📡 Mode: BLE ADVERTISING");
          break;
        case MODE_BT_CONNECTED:
          Serial.println("🔗 Mode: BLE CONNECTED");
          break;
        case MODE_BT_DISCONNECTING:
          Serial.println("🔌 Mode: BLE DISCONNECTING");
          break;
        case MODE_MEMORY_CLEAR:
          Serial.println("🗑️ Mode: MEMORY CLEAR");
          break;
      }
    }
    
    switch(newMode) {
      case MODE_IDLE:
        state.logging = false;
        state.bleActive = false;
        state.bleConnected = false;
        break;
      case MODE_LOGGING:
        state.logging = true;
        break;
      case MODE_BT_ADVERTISING:
        state.bleActive = true;
        break;
      case MODE_BT_CONNECTED:
        state.bleConnected = true;
        state.bleActive = true;
        break;
      case MODE_BT_DISCONNECTING:
        state.bleConnected = false;
        state.disconnectStartTime = millis();
        break;
      case MODE_MEMORY_CLEAR:
        break;
    }
  }
  
  void setPendingDisconnect(bool pending) {
    state.pendingDisconnect = pending;
  }
  
  bool getPendingDisconnect() {
    return state.pendingDisconnect;
  }
  
  void processCommand(String command) {
    command.trim();
    state.lastCommand = command;
    
    if (Serial) {
      Serial.print("📨 Received command: ");
      Serial.println(command);
    }
    
    if (command == CMD_STATUS) {
      String status = generateStatusResponse();
      if (Serial) Serial.println(status);
    }
    else if (command == CMD_MEMORY_STATUS) {
      String memoryStatus = generateMemoryStatus();
      if (Serial) Serial.println(memoryStatus);
    }
    else if (command == CMD_EXTRACT_DATA) {
      if (Serial) Serial.println("📤 Starting data extraction...");
      // This will be handled by BLE handler in the main loop
    }
    else if (command == CMD_CLEAR_MEMORY) {
      if (Serial) Serial.println("🗑️ Clearing memory...");
      flashStorage.clearStorage();
    }
    else if (command == CMD_DUMP_SERIAL) {
      if (Serial) Serial.println("📤 Dumping flash contents to Serial as CSV (time_s)...");
      flashStorage.dumpToSerialSeconds();
    }
    else {
      if (Serial) Serial.println("❌ Unknown command: " + command);
    }
  }
  
  String generateStatusResponse() {
    String status = "STATUS:";
    status += "Mode=" + String(state.mode);
    status += ",Logging=" + String(state.logging ? "true" : "false");
    status += ",BLE=" + String(state.bleConnected ? "connected" : "disconnected");
    status += ",Time=" + String(millis());
    return status;
  }
  
  String generateMemoryStatus() {
    String memoryStatus = "MEMORY:";
    memoryStatus += "TotalSamples=" + String(flashStorage.getTotalSamples());
    memoryStatus += ",Usage=" + String(flashStorage.getUsagePercent()) + "%";
    memoryStatus += ",MaxCapacity=" + String(flashStorage.getMaxCapacity());
    memoryStatus += ",Full=" + String(flashStorage.isFull() ? "true" : "false");
    return memoryStatus;
  }
  
  void generateTelemetryData(const FlightSample &sample) {
    if (!state.logging) return;
    
    // Store in flash memory
    flashStorage.addSample(sample);
    state.dataPoints++;
    
    // Also send live telemetry over BLE (altitude + vertical accel)
    if (state.bleConnected) {
      String telemetry = "TELEMETRY:";
      telemetry += "ALT=" + String(sample.alt_m, 2);
      telemetry += ",ACC=" + String(sample.az_ms2, 2);
      telemetry += ",TIME=" + String(sample.t_ms);
      telemetry += ",POINTS=" + String(state.dataPoints);
      
      Serial.println(telemetry);
    }
  }
  
  DeviceMode getMode() {
    return state.mode;
  }
  
  SystemState getState() {
    return state;
  }
  
  bool isLogging() {
    return state.logging;
  }
  
  void setLogging(bool logging) {
    state.logging = logging;
  }
};

// ==================== BLE HANDLER ====================
class BLEHandler {
private:
  BLEService telemetryService;
  BLECharacteristic dataCharacteristic;
  BLECharacteristic commandCharacteristic;
  String receivedCommand;
  bool newCommandAvailable;
  
  // Flow control variables
  bool dataExportInProgress;
  unsigned long lastDataSendTime;
  const unsigned long DATA_SEND_INTERVAL = 20; // Reduced from 50ms to 20ms for speed
  const unsigned long PROGRESS_INTERVAL = 200; // Send progress every 200 samples
  uint32_t currentExportIndex;
  uint32_t totalSamplesToExport;

public:
  BLEHandler() :
    telemetryService(SERVICE_UUID),
    dataCharacteristic(DATA_UUID, BLERead | BLENotify, 512),
    commandCharacteristic(COMMAND_UUID, BLERead | BLEWrite, 512) {
    
    newCommandAvailable = false;
    receivedCommand = "";
    dataExportInProgress = false;
    lastDataSendTime = 0;
    currentExportIndex = 0;
    totalSamplesToExport = 0;
  }
  
  bool begin(StateManager& stateManager) {
    if (!BLE.begin()) {
      if (Serial) Serial.println("❌ Failed to initialize BLE!");
      return false;
    }
    
    BLE.setDeviceName(DEVICE_NAME);
    BLE.setLocalName(DEVICE_NAME);
    BLE.setAdvertisedService(telemetryService);
    
    telemetryService.addCharacteristic(dataCharacteristic);
    telemetryService.addCharacteristic(commandCharacteristic);
    BLE.addService(telemetryService);
    
    // Set up command callback
    commandCharacteristic.setEventHandler(BLEWritten, [](BLEDevice central, BLECharacteristic characteristic) {
      const uint8_t* data = characteristic.value();
      int length = characteristic.valueLength();
      
      String command = "";
      for (int i = 0; i < length; i++) {
        command += (char)data[i];
      }
      command.trim();
      
      extern BLEHandler bleHandler;
      bleHandler.receivedCommand = command;
      bleHandler.newCommandAvailable = true;
      
      Serial.println("📥 BLE Command received: " + command);
    });
    
    // Set connection event handlers
    BLE.setEventHandler(BLEConnected, [](BLEDevice central) {
      Serial.println("🔗 BLE Connected to central: " + central.address());
    });
    
    BLE.setEventHandler(BLEDisconnected, [](BLEDevice central) {
      Serial.println("🔌 BLE Disconnected from central: " + central.address());
    });
    
    uint8_t initial[] = {'R','E','A','D','Y'};
    dataCharacteristic.writeValue(initial, 5);
    commandCharacteristic.writeValue(initial, 5);
    
    BLE.advertise();
    stateManager.setMode(MODE_BT_ADVERTISING);
    if (Serial) Serial.println("📡 BLE Advertising Started");
    
    return true;
  }
  
  void update(StateManager& stateManager) {
    if (!stateManager.getState().bleActive) return;
    
    BLEDevice central = BLE.central();
    if (central) {
      if (!stateManager.getState().bleConnected) {
        stateManager.setMode(MODE_BT_CONNECTED);
        if (Serial) Serial.println("🔗 BLE Connected to: " + central.address());
        
        // Send initial status
        String status = stateManager.generateStatusResponse();
        dataCharacteristic.writeValue(status.c_str(), status.length());
      }
      
      // Handle controlled data export
      if (dataExportInProgress) {
        unsigned long currentTime = millis();
        if (currentTime - lastDataSendTime >= DATA_SEND_INTERVAL) {
          if (!sendNextDataChunk()) {
            // Export complete
            dataExportInProgress = false;
            sendData("END_DATA_EXPORT");
            delay(30); // Reduced delay
            sendData("DATA_EXPORT_COMPLETE");
            if (Serial) Serial.println("✅ Data export completed successfully");
          }
          lastDataSendTime = currentTime;
        }
      }
      
      // Handle new commands
      if (newCommandAvailable) {
        stateManager.processCommand(receivedCommand);
        
        // Send response
        if (receivedCommand == CMD_STATUS) {
          String status = stateManager.generateStatusResponse();
          dataCharacteristic.writeValue(status.c_str(), status.length());
        }
        else if (receivedCommand == CMD_MEMORY_STATUS) {
          String memoryStatus = stateManager.generateMemoryStatus();
          dataCharacteristic.writeValue(memoryStatus.c_str(), memoryStatus.length());
        }
        else if (receivedCommand == CMD_EXTRACT_DATA) {
          startControlledDataExport();
        }
        
        newCommandAvailable = false;
        receivedCommand = "";
      }
      
      // Send periodic status updates when connected
      static unsigned long lastStatusUpdate = 0;
      if (millis() - lastStatusUpdate > 5000) { // Every 5 seconds
        String status = "DEVICE_ALIVE:Time=" + String(millis());
        dataCharacteristic.writeValue(status.c_str(), status.length());
        lastStatusUpdate = millis();
      }
      
    } else {
      if (stateManager.getState().bleConnected) {
        stateManager.setMode(MODE_BT_DISCONNECTING);
        Serial.println("🔌 BLE Disconnected - no central");
      }
    }
    
    if (stateManager.getState().mode == MODE_BT_DISCONNECTING) {
      if (millis() - stateManager.getState().disconnectStartTime >= BT_DISCONNECT_DURATION) {
        stop(stateManager);
      }
    }
    
    if (stateManager.getPendingDisconnect() && stateManager.getState().bleConnected) {
      Serial.println("🔌 Executing pending disconnect");
      stop(stateManager);
      stateManager.setPendingDisconnect(false);
    }
  }
  
  void startControlledDataExport() {
    dataExportInProgress = true;
    currentExportIndex = 0;
    totalSamplesToExport = flashStorage.getTotalSamples();
    lastDataSendTime = 0;
    
    if (Serial) {
      Serial.println("🚀 Starting FAST controlled data export");
      Serial.print("📊 Total samples to export: ");
      Serial.println(totalSamplesToExport);
    }
    
    // Send start marker
    sendData("BEGIN_DATA_EXPORT");
    delay(30);
    
    // Send header (match flash_storage CSV)
    sendData("t_ms,alt_m,ax_ms2,ay_ms2,az_ms2,gx_rad_s,gy_rad_s,gz_rad_s,temp_C");
    delay(30);
    
    // Send memory status for debugging
    String memStatus = "EXPORT_MEMORY:Samples=" + String(totalSamplesToExport) + ",Full=" + String(flashStorage.isFull() ? "true" : "false");
    sendData(memStatus);
    delay(30);
    
    // If no samples, send completion immediately
    if (totalSamplesToExport == 0) {
      if (Serial) Serial.println("⚠️ No samples to export - flash memory is empty");
      dataExportInProgress = false;
      sendData("END_DATA_EXPORT");
      delay(30);
      sendData("DATA_EXPORT_COMPLETE");
    }
  }

  bool sendNextDataChunk() {
    if (currentExportIndex >= totalSamplesToExport) {
      return false; // No more data
    }

    // Send progress every 200 samples (reduced frequency for speed)
    if (currentExportIndex % PROGRESS_INTERVAL == 0) {
      String progress = "EXPORT_PROGRESS:" + String(currentExportIndex) + "/" + String(totalSamplesToExport);
      sendData(progress);
      if (Serial) Serial.println(progress);
    }

    // Get and send the actual sample data
    FlightSample sample = flashStorage.getSampleAtIndex(currentExportIndex);

    // If we got an "empty" sample (all zeros), treat as end of valid data
    if (sample.t_ms == 0 && sample.alt_m == 0.0f && sample.az_ms2 == 0.0f) {
      return false;
    }
    
    String dataLine = String(sample.t_ms) + "," +
                     String(sample.alt_m, 2) + "," +
                     String(sample.ax_ms2, 2) + "," +
                     String(sample.ay_ms2, 2) + "," +
                     String(sample.az_ms2, 2) + "," +
                     String(sample.gx_rad_s, 3) + "," +
                     String(sample.gy_rad_s, 3) + "," +
                     String(sample.gz_rad_s, 3) + "," +
                     String(sample.temp_C, 2);
    
    sendData(dataLine);
    currentExportIndex++;
    
    return true;
  }
  
  void stop(StateManager& stateManager) {
    if (stateManager.getState().bleActive || stateManager.getState().bleConnected) {
      dataExportInProgress = false; // Stop any ongoing export
      BLE.disconnect();
      BLE.stopAdvertise();
      stateManager.setMode(MODE_IDLE);
      Serial.println("📡 BLE Stopped");
      
      // Visual feedback
      for(int i = 0; i < 4; i++) {
        setLEDColor(255, 0, 0);
        delay(250);
        setLEDColor(0, 0, 0);
        delay(250);
      }
    }
  }
  
  bool isConnected(StateManager& stateManager) {
    return stateManager.getState().bleConnected;
  }
  
  bool isAdvertising(StateManager& stateManager) {
    return stateManager.getState().bleActive;
  }
  
  void sendData(String data) {
    if (data.length() > 0) {
      const int MAX_BLE_DATA = 512;
      uint8_t buffer[MAX_BLE_DATA];
      int length = data.length();
      if (length >= MAX_BLE_DATA) length = MAX_BLE_DATA - 1;
      data.getBytes(buffer, length + 1);
      dataCharacteristic.writeValue(buffer, length);
      if (Serial) Serial.println("📤 BLE Sent: " + data);
    }
  }
  
  String getReceivedCommand() {
    newCommandAvailable = false;
    return receivedCommand;
  }
  
  bool hasNewCommand() {
    return newCommandAvailable;
  }
  
  void clearCommand() {
    newCommandAvailable = false;
    receivedCommand = "";
  }
};

// ==================== BUTTON HANDLER ====================
struct ButtonState {
  bool pressed;
  bool lastState;
  unsigned long pressStartTime;
  unsigned long lastReleaseTime;
  int pressCount;
};

class ButtonHandler {
private:
  void handlePressPattern(int count, StateManager& stateManager, BLEHandler& bleHandler) {
    if (Serial) Serial.println("🔘 Processing pattern: " + String(count) + " presses");
    
    switch(count) {
      case 1:
        // Toggle logging - only works when BLE is disconnected
        if (!stateManager.getState().bleConnected) {
          if (stateManager.isLogging()) {
            stateManager.setLogging(false);
            stateManager.setMode(MODE_IDLE);
            Serial.println("🔵 Logging Stopped via button");
          } else {
            stateManager.setLogging(true);
            stateManager.setMode(MODE_LOGGING);
            Serial.println("🟢 Logging Started via button");
          }
        } else {
          Serial.println("ℹ️  Cannot toggle logging while BLE connected");
        }
        break;
        
      case 2:
        // Toggle Bluetooth
        if (!stateManager.getState().bleActive) {
          if (bleHandler.begin(stateManager)) {
            Serial.println("📡 BLE Started via button");
          }
        } else {
          if (!stateManager.getState().bleConnected) {
            bleHandler.stop(stateManager);
            Serial.println("📡 BLE Stopped via button");
          } else {
            stateManager.setPendingDisconnect(true);
            Serial.println("🔌 BLE Disconnect Pending via button");
          }
        }
        break;
        
      case 3:
        // Force disconnect BLE
        if (stateManager.getState().bleActive || stateManager.getState().bleConnected) {
          bleHandler.stop(stateManager);
          Serial.println("🔌 BLE Force Disconnected via button");
        }
        break;
    }
  }
  
  void handleMemoryClearProgress(unsigned long holdDuration) {
    float progress = static_cast<float>(holdDuration - 1000) / (LONG_PRESS_THRESHOLD - 1000);
    progress = constrain(progress, 0.0f, 1.0f);
    float period = 1200.0f - (progress * 1000.0f);
    float intensity = 0.3f + (progress * 0.7f);
    
    unsigned long cycleTime = millis() % static_cast<unsigned long>(period);
    float pulsePhase = static_cast<float>(cycleTime) / period;
    
    float pulseValue = (pulsePhase < 0.3f) ? 1.0f : 0.0f;
    uint8_t brightness = static_cast<uint8_t>(intensity * 255 * pulseValue);
    setLEDColor(brightness, brightness, 0);
  }

public:
  ButtonState state;
  
  ButtonHandler() {
    state.pressed = false;
    state.lastState = HIGH;
    state.pressStartTime = 0;
    state.lastReleaseTime = 0;
    state.pressCount = 0;
  }
  
  void begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    Serial.println("✅ Button Handler Initialized");
  }
  
  void update(StateManager& stateManager, BLEHandler& bleHandler) {
    unsigned long currentTime = millis();
    int currentButtonState = digitalRead(BUTTON_PIN);
    
    if (currentButtonState == LOW && state.lastState == HIGH) {
      state.pressed = true;
      state.pressStartTime = currentTime;
      state.lastState = LOW;
      Serial.println("🔘 Button PRESSED");
    }
    
    if (currentButtonState == HIGH && state.lastState == LOW) {
      state.pressed = false;
      unsigned long pressDuration = currentTime - state.pressStartTime;
      state.lastState = HIGH;
      
      Serial.println("🔘 Button RELEASED after " + String(pressDuration) + "ms");
      
      if (pressDuration >= LONG_PRESS_THRESHOLD) {
        Serial.println("🗑️ LONG PRESS - Memory Cleared!");
        for (int i = 0; i < 3; i++) {
          setLEDColor(255, 255, 255); delay(200);
          setLEDColor(0, 0, 0); delay(200);
        }
        stateManager.setMode(MODE_IDLE);
        stateManager.setLogging(false);
        flashStorage.clearStorage();
        bleHandler.stop(stateManager);
        state.pressCount = 0;
        return;
      }
      
      if (pressDuration < LONG_PRESS_THRESHOLD) {
        if (currentTime - state.lastReleaseTime < MULTI_PRESS_WINDOW) {
          state.pressCount++;
          Serial.println("🔘 Press count: " + String(state.pressCount));
        } else {
          state.pressCount = 1;
        }
        state.lastReleaseTime = currentTime;
      }
    }
    
    if (state.pressed) {
      unsigned long holdDuration = currentTime - state.pressStartTime;
      if (holdDuration > 1000 && holdDuration <= LONG_PRESS_THRESHOLD) {
        handleMemoryClearProgress(holdDuration);
        return;
      }
    }
    
    if (!state.pressed && state.pressCount > 0 && 
        (currentTime - state.lastReleaseTime) > MULTI_PRESS_WINDOW) {
      int count = state.pressCount;
      state.pressCount = 0;
      handlePressPattern(count, stateManager, bleHandler);
    }
  }
};

// ==================== LED FUNCTIONS ====================
void setLEDColor(uint8_t red, uint8_t green, uint8_t blue) {
  analogWrite(LED_RED, 255 - red);
  analogWrite(LED_GREEN, 255 - green);
  analogWrite(LED_BLUE, 255 - blue);
}

float smoothWave(unsigned long time, float periodMs) {
  float phase = fmod(static_cast<float>(time), periodMs) / periodMs;
  return 0.5f * (1.0f - cos(2.0f * PI * phase));
}

void updateLED(StateManager& stateManager, ButtonHandler& buttonHandler) {
  unsigned long currentTime = millis();
  
  if (buttonHandler.state.pressed) {
    unsigned long holdDuration = currentTime - buttonHandler.state.pressStartTime;
    if (holdDuration > 1000 && holdDuration <= LONG_PRESS_THRESHOLD) {
      return;
    }
  }
  
  switch(stateManager.getMode()) {
    case MODE_IDLE: {
      float val = smoothWave(currentTime, 2000.0f);
      setLEDColor(0, 0, static_cast<uint8_t>(val * 50));
      break;
    }
    case MODE_LOGGING: {
      float val = smoothWave(currentTime, 1500.0f);
      setLEDColor(0, static_cast<uint8_t>(val * 255), 0);
      break;
    }
    case MODE_BT_ADVERTISING: {
      unsigned long cycle = currentTime % 1000;
      setLEDColor(0, 0, (cycle < 300) ? 255 : 0);
      break;
    }
    case MODE_BT_CONNECTED: {
      float val = smoothWave(currentTime, 3000.0f);
      setLEDColor(0, static_cast<uint8_t>(val * 150), static_cast<uint8_t>(val * 255));
      break;
    }
    case MODE_BT_DISCONNECTING: {
      unsigned long cycle = currentTime % 1000;
      setLEDColor((cycle < 500) ? 255 : 0, 0, 0);
      break;
    }
    case MODE_MEMORY_CLEAR:
      break;
  }
}

// ==================== SENSOR GLOBALS ====================
Adafruit_LSM6DSO32 lsm6dso32;
Adafruit_BMP3XX    bmp;
static bool sensorsInitialized = false;

static bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

static bool initSensors() {
  Wire.begin();
  Wire.setClock(400000); // fast I2C

  // Initialize LSM6DSO32
  if (!lsm6dso32.begin_I2C(LSM6DSO32_ADDR, &Wire)) {
    if (Serial) {
      Serial.println("❌ Failed to find external LSM6DSO32 at expected address!");
      Serial.println("   Check wiring and SDO pin.");
    }
    return false;
  }
  lsm6dso32.setAccelRange(LSM6DSO32_ACCEL_RANGE_8_G);
  lsm6dso32.setGyroRange(LSM6DS_GYRO_RANGE_2000_DPS);
  lsm6dso32.setAccelDataRate(LSM6DS_RATE_104_HZ);
  lsm6dso32.setGyroDataRate(LSM6DS_RATE_104_HZ);

  // Initialize BMP390
  if (!bmp.begin_I2C(BMP3XX_ADDR, &Wire)) {
    if (Serial) {
      Serial.println("❌ Failed to find BMP390 at expected address!");
      Serial.println("   Check wiring and SDO pin.");
    }
    return false;
  }
  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP3_ODR_50_HZ);

  if (Serial) {
    Serial.println("✅ LSM6DSO32 + BMP390 initialized");
  }
  return true;
}

static bool readSensorsIntoSample(FlightSample &sample) {
  if (!sensorsInitialized) {
    return false;
  }

  // Check that devices are still present
  if (!i2cDevicePresent(LSM6DSO32_ADDR) || !i2cDevicePresent(BMP3XX_ADDR)) {
    static unsigned long lastWarn = 0;
    if (Serial && millis() - lastWarn > 1000) {
      Serial.println("⚠️ Sensor disconnected - skipping sample");
      lastWarn = millis();
    }
    return false;
  }

  sensors_event_t accel, gyro, temp;
  lsm6dso32.getEvent(&accel, &gyro, &temp);

  if (!bmp.performReading()) {
    static unsigned long lastWarn = 0;
    if (Serial && millis() - lastWarn > 1000) {
      Serial.println("⚠️ BMP390 read failed - skipping sample");
      lastWarn = millis();
    }
    return false;
  }

  sample.t_ms   = millis();
  sample.alt_m  = bmp.readAltitude(SEALEVELPRESSURE_HPA);
  sample.ax_ms2 = accel.acceleration.x;
  sample.ay_ms2 = accel.acceleration.y;
  sample.az_ms2 = accel.acceleration.z;
  sample.gx_rad_s = gyro.gyro.x;
  sample.gy_rad_s = gyro.gyro.y;
  sample.gz_rad_s = gyro.gyro.z;
  sample.temp_C   = bmp.temperature;

  return true;
}

// ==================== GLOBAL INSTANCES ====================
StateManager stateManager;
BLEHandler bleHandler;
ButtonHandler buttonHandler;

// ==================== MAIN PROGRAM ====================
unsigned long lastTelemetryTime = 0;
unsigned long lastBLEUpdate = 0;
const unsigned long BLE_UPDATE_INTERVAL = 100;
const unsigned long TELEMETRY_INTERVAL = 1000 / SAMPLE_RATE_HZ;

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setLEDColor(0, 0, 0);
  
  // Startup LED sequence to indicate independent operation
  setLEDColor(0, 0, 50);
  delay(300);
  setLEDColor(0, 0, 0);
  delay(100);
  setLEDColor(0, 0, 30);
  delay(300);
  setLEDColor(0, 0, 0);
  
  Serial.begin(115200);
  // Wait for serial with timeout for independent operation
  unsigned long serialTimeout = millis() + 2000; // 2 second timeout
  while (!Serial && millis() < serialTimeout) {
    delay(10);
  }
  delay(100);
  
  // Only print startup info if serial is available
  if (Serial) {
    Serial.println();
    Serial.println("🚀 Rocket Telemetry System - Flash Storage Edition");
    Serial.println("===================================================");
    Serial.println("✅ System Initialized");
    Serial.println("📡 BLE Device: " + String(DEVICE_NAME));
    Serial.println("💾 Flash Storage: Ready");
    Serial.println();
    Serial.println("📋 Button Guide:");
    Serial.println("   Single Press: Toggle Logging (when BLE disconnected)");
    Serial.println("   Double Press: Toggle Bluetooth");
    Serial.println("   Triple Press: Force Disconnect BLE");
    Serial.println("   Long Press (10s): Clear Memory");
    Serial.println("===================================================");
  }
  
  stateManager.begin();
  buttonHandler.begin();
  flashStorage.begin();

  // Initialize external sensors
  sensorsInitialized = initSensors();
  if (!sensorsInitialized && Serial) {
    Serial.println("⚠️ Sensors NOT initialized - logging will be disabled");
  }

  stateManager.setMode(MODE_IDLE);
  
  // Final LED indication that system is ready for independent operation
  setLEDColor(0, 0, 20);
}

void loop() {
  unsigned long currentTime = millis();

  // Check for Serial commands (typed in Serial Monitor)
  if (Serial && Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      stateManager.processCommand(cmd);
    }
  }
  
  buttonHandler.update(stateManager, bleHandler);
  
  if (currentTime - lastBLEUpdate >= BLE_UPDATE_INTERVAL) {
    bleHandler.update(stateManager);
    lastBLEUpdate = currentTime;
  }
  
  updateLED(stateManager, buttonHandler);
  
  // Generate and store telemetry data when logging
  if (stateManager.isLogging() && sensorsInitialized) {
    if (currentTime - lastTelemetryTime >= TELEMETRY_INTERVAL) {
      FlightSample sample = {};
      if (readSensorsIntoSample(sample)) {
        stateManager.generateTelemetryData(sample);
      }
      lastTelemetryTime = currentTime;
    }
  }
  
  delay(MAIN_LOOP_DELAY);
}
