#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Wire.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_BMP3XX.h>
#include "FlashStorage.h"

#define SEALEVELPRESSURE_HPA (1013.25)

// Hardware pins
const int BUTTON_PIN   = 5;    // Start/stop / BLE / clear button (active LOW, INPUT_PULLUP)
const int LED_R_PIN    = LEDR;
const int LED_G_PIN    = LEDG;
const int LED_B_PIN    = LEDB;

// IMU + baro objects
Adafruit_LSM6DSO32 lsm6;
Adafruit_BMP3XX    bmp;

// Logging control
bool loggingEnabled          = false;
unsigned long lastSampleTime = 0;
unsigned long logStart_ms    = 0;   // time zero for this logging session
unsigned long ignoreSerialUntil = 0;  // ignore junk for first 1 second

// Sample rate (50 samples per second)
const uint16_t SAMPLE_RATE_HZ     = 50;
const uint16_t SAMPLE_INTERVAL_MS = 1000 / SAMPLE_RATE_HZ;

// Button handling
const unsigned long BUTTON_DEBOUNCE_MS      = 50;
const unsigned long DOUBLE_PRESS_WINDOW_MS  = 400;
const unsigned long CLEAR_HOLD_MS           = 5000;

struct ButtonState {
  int  rawState;
  int  stableState;
  int  lastStableState;
  unsigned long lastChangeMs;
  unsigned long pressStartMs;
  unsigned long lastReleaseMs;
  uint8_t pressCount;
  bool longPressHandled;
};

ButtonState buttonState = {
  HIGH, HIGH, HIGH,
  0, 0, 0,
  0,
  false
};

// BLE state
bool bleInitialized  = false;
bool bleAdvertising  = false;
bool bleConnected    = false;

// Cached flash sample count for MEMORY: line
uint32_t g_totalSamplesCached = 0;

BLEService controlService("19B10000-E8F2-537E-4F6C-D104768A1214");
// RX: host writes commands (e.g. 'D' for dump)
BLECharacteristic bleRxChar(
  "19B10001-E8F2-537E-4F6C-D104768A1214",
  BLEWrite | BLEWriteWithoutResponse,
  20
);
// TX: device notifies CSV/text lines back to host
BLECharacteristic bleTxChar(
  "19B10002-E8F2-537E-4F6C-D104768A1214",
  BLENotify | BLERead,
  64
);

// Status
bool clearInProgress = false;

// ---------------- LED helpers ----------------

void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  // Built-in RGB LED is active LOW on Nano 33 BLE
  analogWrite(LED_R_PIN, 255 - r);
  analogWrite(LED_G_PIN, 255 - g);
  analogWrite(LED_B_PIN, 255 - b);
}

void ledOff() {
  setRgb(0, 0, 0);
}

// ---------------- Serial helpers ----------------

void printHelp() {
  Serial.println();
  Serial.println(F("=== Altimeter Logger (FLASH + Serial + BLE button control) ==="));
  Serial.println(F("Serial commands (1 char + Enter):"));
  Serial.println(F("  A -> START logging to flash (50 samples/s)"));
  Serial.println(F("  B -> STOP logging"));
  Serial.println(F("  S -> STATUS (shows flash usage)"));
  Serial.println(F("  C -> CLEAR flash"));
  Serial.println(F("  D -> DUMP all stored samples"));
  Serial.println(F("  H -> HELP"));
  Serial.println();
  Serial.println(F("Button patterns:"));
  Serial.println(F("  1x short press : Toggle logging"));
  Serial.println(F("  2x short press : Toggle BLE advertising ON/OFF"));
  Serial.println(F("  Hold ~5 s      : Clear flash (red LED while clearing)"));
  Serial.println();
}

void printStatus() {
  Serial.println(F("=== STATUS ==="));
  Serial.print(F("Logging: "));
  Serial.println(loggingEnabled ? F("ON") : F("OFF"));
  Serial.print(F("BLE:     "));
  if (!bleInitialized) Serial.println(F("OFF (not initialised)"));
  else if (bleConnected) Serial.println(F("CONNECTED"));
  else if (bleAdvertising) Serial.println(F("ADVERTISING"));
  else Serial.println(F("IDLE"));
  flashStorage.printStatus();

  // Compact memory status line that desktop software can parse easily.
  // Example: MEMORY:totalSamples=1234,capacity=17280
  g_totalSamplesCached = flashStorage.getTotalSamples();
  Serial.print(F("MEMORY:totalSamples="));
  Serial.print(g_totalSamplesCached);
  Serial.print(F(",capacity="));
  Serial.println(TOTAL_SAMPLES);
}

// ---------------- Sensors ----------------

bool initSensors() {
  Wire.begin();
  Wire.setClock(400000);

  if (!lsm6.begin_I2C(0x6A, &Wire)) {
    Serial.println(F("LSM6DSO32 not found"));
    return false;
  }
  lsm6.setAccelRange(LSM6DSO32_ACCEL_RANGE_8_G);
  lsm6.setAccelDataRate(LSM6DS_RATE_104_HZ);

  if (!bmp.begin_I2C(0x77, &Wire)) {
    Serial.println(F("BMP390 not found"));
    return false;
  }
  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP3_ODR_50_HZ);

  Serial.println(F("Sensors initialized"));
  return true;
}

// ---------------- BLE helpers ----------------

// Helper used by FlashStorage BLE dump callback to send one text line over TX
void bleSendLine(const char* line, void* /*ctx*/) {
  if (!bleInitialized) return;
  if (!line || !*line) return;

  char buf[80];
  size_t len = strlen(line);
  if (len > sizeof(buf) - 2) {
    len = sizeof(buf) - 2;
  }
  memcpy(buf, line, len);
  buf[len] = '\n';
  buf[len + 1] = '\0';

  bleTxChar.writeValue((const uint8_t*)buf, len + 1);
  delay(2); // small pacing to avoid flooding
}

void onBleConnected(BLEDevice central) {
  bleConnected = true;
  Serial.print(F("BLE connected: "));
  Serial.println(central.address());
}

void onBleDisconnected(BLEDevice central) {
  (void)central;
  bleConnected = false;
  Serial.println(F("BLE disconnected"));
}

bool ensureBleInitialised() {
  if (bleInitialized) return true;

  if (!BLE.begin()) {
    Serial.println(F("BLE init failed"));
    return false;
  }

  BLE.setDeviceName("Altimeter");
  BLE.setLocalName("Altimeter");
  BLE.setAdvertisedService(controlService);

  // Attach characteristics to the control service
  controlService.addCharacteristic(bleRxChar);
  controlService.addCharacteristic(bleTxChar);
  BLE.addService(controlService);

  BLE.setEventHandler(BLEConnected, onBleConnected);
  BLE.setEventHandler(BLEDisconnected, onBleDisconnected);

  bleInitialized = true;
  Serial.println(F("BLE stack initialised"));
  return true;
}

// Forward-declare so BLE handlers can call it before its full definition.
void processSerialCommand(char c);

void onBleRxWritten(BLEDevice central, BLECharacteristic characteristic) {
  (void)central;
  int len = characteristic.valueLength();
  if (len <= 0) return;

  uint8_t data[20];
  if (len > (int)sizeof(data)) len = sizeof(data);
  characteristic.readValue(data, len);

  char cmd = toupper((char)data[0]);
  Serial.print(F("BLE RX CMD: "));
  Serial.println(cmd);

  switch (cmd) {
    case 'S': {
      // Status: print to Serial AND send a compact MEMORY line over BLE
      processSerialCommand(cmd);  // updates g_totalSamplesCached via printStatus()

      char buf[64];
      snprintf(
        buf,
        sizeof(buf),
        "MEMORY:totalSamples=%lu,capacity=%lu",
        (unsigned long)g_totalSamplesCached,
        (unsigned long)TOTAL_SAMPLES
      );
      bleSendLine(buf, nullptr);
      break;
    }

    case 'A':
    case 'B':
    case 'C':
    case 'H':
      // Reuse existing command handler so LED + logging behaviour stay consistent.
      processSerialCommand(cmd);
      break;

    case 'D':
      // Dump flash contents over BLE (CSV via TX characteristic)
      Serial.println(F("BLE RX: 'D' received, dumping flash over BLE"));
      flashStorage.dumpToCallback(bleSendLine, nullptr);
      break;

    default:
      // Ignore other bytes; command set remains A,B,S,C,D,H only.
      break;
  }
}

void startBleAdvertising() {
  if (!ensureBleInitialised()) return;

  bleRxChar.setEventHandler(BLEWritten, onBleRxWritten);

  BLE.advertise();
  bleAdvertising = true;
  Serial.println(F("BLE advertising ON"));
}

void stopBle() {
  if (!bleInitialized) return;
  BLE.stopAdvertise();
  BLE.disconnect();
  bleAdvertising = false;
  bleConnected = false;
  Serial.println(F("BLE OFF"));
}

// ---------------- Logging helpers ----------------

void toggleLogging() {
  loggingEnabled = !loggingEnabled;

  if (loggingEnabled) {
    lastSampleTime = 0;
    logStart_ms    = millis();
    Serial.println(F("Logging to FLASH STARTED (button)"));
  } else {
    Serial.println(F("Logging STOPPED (button)"));
  }
}

void handleClearFlash() {
  Serial.println(F("Clearing storage (long press)..."));

  loggingEnabled = false;

  // Remember whether BLE was active so we can restore advertising afterwards
  bool wasBleActive = (bleAdvertising || bleConnected);
  if (wasBleActive) {
    stopBle();
  }

  clearInProgress = true;
  setRgb(255, 64, 0);  // amber

  flashStorage.clearStorage();

  clearInProgress = false;

  // Success indication: three green flashes
  for (int i = 0; i < 3; ++i) {
    setRgb(0, 255, 0);
    delay(150);
    ledOff();
    delay(150);
  }

  Serial.println(F("Storage clear complete"));

  // Notify host software that memory is now empty
  Serial.println(F("MEMORY_CLEARED"));
  if (bleInitialized) {
    bleSendLine("MEMORY_CLEARED", nullptr);
  }

  // If BLE was active before clearing, resume advertising so the PC can reconnect
  if (wasBleActive) {
    startBleAdvertising();
  }

  // Do NOT auto‑restart logging; user can press button again if desired
  loggingEnabled = false;
}

void toggleBleFromButton() {
  if (bleAdvertising || bleConnected) {
    stopBle();
  } else {
    startBleAdvertising();
  }
}

// ---------------- Button handling ----------------

void handleShortPressPattern(uint8_t count) {
  if (count == 1) {
    toggleLogging();
  } else if (count == 2) {
    toggleBleFromButton();
  }
}

void updateButton(unsigned long now) {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != buttonState.rawState) {
    buttonState.rawState    = reading;
    buttonState.lastChangeMs = now;
  }

  // Debounce to update stable state
  if ((now - buttonState.lastChangeMs) > BUTTON_DEBOUNCE_MS &&
      buttonState.stableState != buttonState.rawState) {
    buttonState.stableState    = buttonState.rawState;
    buttonState.lastChangeMs   = now;

    if (buttonState.stableState == LOW) {
      // Button pressed
      buttonState.pressStartMs    = now;
      buttonState.longPressHandled = false;
    } else {
      // Button released
      unsigned long pressDuration = now - buttonState.pressStartMs;
      if (!buttonState.longPressHandled && pressDuration < CLEAR_HOLD_MS) {
        // Short press; accumulate for multi‑press
        if (now - buttonState.lastReleaseMs <= DOUBLE_PRESS_WINDOW_MS) {
          buttonState.pressCount++;
        } else {
          buttonState.pressCount = 1;
        }
        buttonState.lastReleaseMs = now;
      }
    }

    buttonState.lastStableState = buttonState.stableState;
  }

  // Long‑press detection for clear
  if (buttonState.stableState == LOW && !buttonState.longPressHandled) {
    unsigned long held = now - buttonState.pressStartMs;
    if (held >= CLEAR_HOLD_MS) {
      buttonState.longPressHandled = true;
      buttonState.pressCount = 0;  // cancel any multi‑press sequence
      handleClearFlash();
    }
  }

  // Evaluate multi‑press when window elapsed and button is up
  if (buttonState.stableState == HIGH &&
      buttonState.pressCount > 0 &&
      (now - buttonState.lastReleaseMs) > DOUBLE_PRESS_WINDOW_MS) {
    uint8_t count = buttonState.pressCount;
    buttonState.pressCount = 0;
    handleShortPressPattern(count);
  }
}

// ---------------- LED state machine ----------------

void updateStatusLED(unsigned long now) {
  if (clearInProgress) {
    // While clearing we just show solid amber/red, regardless of BLE state
    setRgb(255, 80, 0);
    return;
  }

  // Disconnected idle: LED off
  if (!loggingEnabled && !bleAdvertising && !bleConnected) {
    ledOff();
    return;
  }

  // Connected to software: distinct solid blue pattern
  if (bleConnected) {
    if (loggingEnabled) {
      // Logging + connected: cyan (green+blue)
      setRgb(0, 200, 255);
    } else {
      // Connected only: solid blue
      setRgb(0, 0, 255);
    }
    return;
  }

  // Advertising but not connected yet: slow blue blink
  if (bleAdvertising && !bleConnected) {
    unsigned long phase = now % 800; // 0..799
    if (phase < 200) {
      setRgb(0, 0, 255);
    } else {
      ledOff();
    }
    return;
  }

  // Fallback: logging only (no BLE)
  if (loggingEnabled) {
    setRgb(0, 255, 0);
    return;
  }
}

// ---------------- Serial command handling ----------------

void processSerialCommand(char c) {
  c = toupper(static_cast<unsigned char>(c));

  switch (c) {
    case 'A':
      loggingEnabled = true;
      lastSampleTime = 0;
      logStart_ms    = millis();
      Serial.println(F("Logging to FLASH STARTED"));
      break;

    case 'B':
      loggingEnabled = false;
      Serial.println(F("Logging STOPPED"));
      break;

    case 'S':
      printStatus();
      break;

    case 'C':
      handleClearFlash();
      break;

    case 'D':
      flashStorage.dumpToSerialSeconds();
      break;

    case 'H':
      printHelp();
      break;

    default:
      Serial.print(F("Unknown command: "));
      Serial.println(c);
      Serial.println(F("Use A=START, B=STOP, S=STATUS, H=HELP"));
      break;
  }
}

// ---------------- Arduino setup / loop ----------------

void setup() {
  Serial.begin(115200);
  delay(200);  // let USB settle

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  ledOff();

  // Ignore any garbage for the first 1 second after reset
  ignoreSerialUntil = millis() + 1000;

  Serial.println();
  printHelp();

  if (!initSensors()) {
    Serial.println(F("Sensor init failed. Logging disabled."));
  }

  flashStorage.begin();
}

void loop() {
  unsigned long now = millis();

  // --- Button handling (logging / BLE / clear) ---
  updateButton(now);

  // --- Heartbeat on serial every few seconds ---
  static unsigned long lastHeartbeat = 0;
  if (now - lastHeartbeat > 5000) {
    lastHeartbeat = now;
    Serial.println(F("[HB] loop alive"));
  }

  // --- Serial commands ---
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (millis() < ignoreSerialUntil) {
      continue; // ignore startup junk
    }

    if (c == '\r' || c == '\n') {
      continue; // ignore CR/LF
    }

    Serial.print(F("RX CMD: "));
    Serial.println(c);
    processSerialCommand(c);
  }

  // --- Logging ---
  if (loggingEnabled) {
    if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
      lastSampleTime = now;

      sensors_event_t accel, gyro, temp;
      lsm6.getEvent(&accel, &gyro, &temp);

      if (!bmp.performReading()) {
        Serial.println(F("BMP390 read failed"));
      } else {
        float altitude = bmp.readAltitude(SEALEVELPRESSURE_HPA);  // barometric altitude
        float ax       = accel.acceleration.x;
        float ay       = accel.acceleration.y;
        float az       = accel.acceleration.z;

        // Time in seconds since START was issued
        float time_s = (now - logStart_ms) / 1000.0f;

        // Store into FLASH (persistent across power cycles)
        flashStorage.addSample(time_s, altitude, ax, ay, az);
      }
    }
  }

  // --- BLE polling ---
  if (bleAdvertising || bleConnected) {
    BLE.poll();
  }

  // --- LED status ---
  updateStatusLED(now);
}
