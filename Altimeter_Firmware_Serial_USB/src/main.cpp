#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_BMP3XX.h>
#include "FlashStorage.h"

#define SEALEVELPRESSURE_HPA (1013.25)


// Hardware pins
const int BUTTON_PIN   = 5;    // Start/stop / clear button (active LOW, INPUT_PULLUP)
const int LED_R_PIN    = LEDR;
const int LED_G_PIN    = LEDG;
const int LED_B_PIN    = LEDB;

// IMU + baro objects
Adafruit_LSM6DSO32 lsm6;
Adafruit_BMP3XX    bmp;

// Logging control
bool loggingEnabled             = false;
unsigned long lastSampleTime    = 0;
unsigned long logStart_ms       = 0;   // time zero for this logging session
unsigned long ignoreSerialUntil = 0;  // ignore junk for first 1 second

// Set when we hit the flash sampling capacity; used to stop logging and
// show a distinct LED error pattern so the user knows the log is full.
bool sampleOverflow = false;

// Configurable sample rate (Hz). Default 50 Hz.
uint16_t g_sampleRateHz     = 50;
uint16_t g_sampleIntervalMs = 1000 / 50;

// Small config block stored in a reserved flash sector (separate from
// the circular logging area) so the selected sample rate persists
// across power cycles.
struct SampleRateConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t sampleRateHz;
  uint32_t reserved;    // padding / future‑use
};

static const uint32_t SAMPLE_RATE_CONFIG_MAGIC   = 0x52425852; // 'RBXR'
static const uint16_t SAMPLE_RATE_CONFIG_VERSION = 1;

static void loadSampleRateFromFlash();
static void saveSampleRateToFlash();

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

// Cached flash sample count for MEMORY: line
uint32_t g_totalSamplesCached = 0;

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
  Serial.println(F("=== Altimeter Logger (FLASH + USB Serial) ==="));
  Serial.println(F("Serial commands (1 char + Enter):"));
  Serial.println(F("  A -> START logging to flash (current sample rate)"));
  Serial.println(F("  B -> STOP logging"));
  Serial.println(F("  S -> STATUS (shows flash usage)"));
  Serial.println(F("  C -> CLEAR flash"));
  Serial.println(F("  D -> DUMP all stored samples"));
  Serial.println(F("  H -> HELP"));
  Serial.println();
  Serial.println(F("Button patterns:"));
  Serial.println(F("  1x short press : Toggle logging"));
  Serial.println(F("  Hold ~5 s      : Clear flash (red LED while clearing)"));
  Serial.println();
}

void printStatus() {
  Serial.println(F("=== STATUS ==="));
  Serial.print(F("Logging: "));
  Serial.println(loggingEnabled ? F("ON") : F("OFF"));
  flashStorage.printStatus();

  // Compact memory status line that desktop software can parse easily.
  // Example: MEMORY:totalSamples=1234,capacity=17280
  g_totalSamplesCached = flashStorage.getTotalSamples();
  Serial.print(F("MEMORY:totalSamples="));
  Serial.print(g_totalSamplesCached);
  Serial.print(F(",capacity="));
  Serial.println(TOTAL_SAMPLES);

  // Configuration line: current sample rate in Hz
  Serial.print(F("CONFIG:sampleRateHz="));
  Serial.println(g_sampleRateHz);
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

// ---------------- Serial helpers ----------------

static void setSampleRateHz(uint16_t requestedHz) {
  uint16_t newHz;
  if (requestedHz <= 10) {
    newHz = 10;
  } else if (requestedHz <= 25) {
    newHz = 25;
  } else {
    newHz = 50;
  }
  g_sampleRateHz     = newHz;
  g_sampleIntervalMs = 1000 / g_sampleRateHz;

  // Persist the new rate so it is restored after power cycles.
  saveSampleRateToFlash();

  Serial.print(F("CONFIG:sampleRateHz="));
  Serial.println(g_sampleRateHz);
}

// Compute the address of the small config sector we use to store
// persistent settings (currently just the sample rate). This lives
// immediately after the circular logging area managed by FlashStorage
// so it is never touched by clearStorage() or normal logging.
static bool getSampleRateConfigAddress(uint32_t &configAddr, uint32_t &sectorSize) {
  FlashIAP flash;
  if (flash.init() != 0) {
    return false;
  }

  uint32_t storageBase = flashStorage.getStorageBaseAddr();
  sectorSize           = flash.get_sector_size(storageBase);
  uint32_t flashStart  = flash.get_flash_start();
  uint32_t flashSize   = flash.get_flash_size();

  uint32_t dataRegionSize = SECTORS_COUNT * sectorSize;
  uint32_t addr           = storageBase + dataRegionSize;

  // Ensure there is at least one full sector available for config.
  if (addr + sectorSize > flashStart + flashSize) {
    flash.deinit();
    return false;
  }

  configAddr = addr;
  flash.deinit();
  return true;
}

static void loadSampleRateFromFlash() {
  uint32_t cfgAddr = 0;
  uint32_t sectorSize = 0;
  if (!getSampleRateConfigAddress(cfgAddr, sectorSize)) {
    return; // fall back to compile-time default
  }

  FlashIAP flash;
  if (flash.init() != 0) {
    return;
  }

  SampleRateConfig cfg;
  if (flash.read(&cfg, cfgAddr, sizeof(cfg)) != 0) {
    flash.deinit();
    return;
  }
  flash.deinit();

  if (cfg.magic != SAMPLE_RATE_CONFIG_MAGIC ||
      cfg.version != SAMPLE_RATE_CONFIG_VERSION) {
    return; // no valid config stored yet
  }

  uint16_t sr = cfg.sampleRateHz;
  if (sr != 10 && sr != 25 && sr != 50) {
    return; // ignore nonsensical values
  }

  g_sampleRateHz     = sr;
  g_sampleIntervalMs = 1000 / g_sampleRateHz;
}

static void saveSampleRateToFlash() {
  uint32_t cfgAddr = 0;
  uint32_t sectorSize = 0;
  if (!getSampleRateConfigAddress(cfgAddr, sectorSize)) {
    return;
  }

  FlashIAP flash;
  if (flash.init() != 0) {
    return;
  }

  SampleRateConfig cfg;
  cfg.magic        = SAMPLE_RATE_CONFIG_MAGIC;
  cfg.version      = SAMPLE_RATE_CONFIG_VERSION;
  cfg.sampleRateHz = g_sampleRateHz;
  cfg.reserved     = 0;

  // Erase the whole sector that contains the config block, then
  // program just the struct at the start of that sector.
  if (flash.erase(cfgAddr, sectorSize) != 0) {
    flash.deinit();
    return;
  }

  if (flash.program(&cfg, cfgAddr, sizeof(cfg)) != 0) {
    flash.deinit();
    return;
  }

flash.deinit();
}

// Helper hook used by FlashStorage during long dumps. BLE is no longer
// used, so this is a no-op stub that simply allows the logger to call
// bleYield() without pulling in any wireless stacks.
void bleYield() {}

// ---------------- Logging helpers ----------------

void toggleLogging() {
  loggingEnabled = !loggingEnabled;

  if (loggingEnabled) {
    // Starting a new logging session clears any previous overflow state.
    sampleOverflow = false;
    lastSampleTime = 0;
    logStart_ms    = millis();
    Serial.println(F("Logging to FLASH STARTED (button)"));
  } else {
    Serial.println(F("Logging STOPPED (button)"));
  }
}

void handleClearFlash() {
  Serial.println(F("Clearing storage (long press)..."));

  loggingEnabled  = false;
  sampleOverflow  = false;  // clear any previous overflow error state

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

  // Do NOT auto‑restart logging; user can press button again if desired
  loggingEnabled = false;
}


// ---------------- Button handling ----------------

void handleShortPressPattern(uint8_t count) {
  // Any short-press pattern now simply toggles logging. The previous
// previous double-press behavior is no longer used.
  (void)count;
  toggleLogging();
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

  // Evaluate multi‑press when window elapsed and button is up. We still
  // keep the small multi-press window for responsiveness, but regardless
  // of how many short presses occurred, the behaviour is just a single
  // "toggle logging" action.
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
// While clearing we just show solid amber/red, regardless of other state
    setRgb(255, 80, 0);
    return;
  }

  if (sampleOverflow) {
    // Distinct fast red blink pattern when the sampling capacity has
    // been reached and logging was stopped automatically.
    unsigned long phase = now % 400; // 0..399
    if (phase < 200) {
      setRgb(255, 0, 0);
    } else {
      ledOff();
    }
    return;
  }

  // Disconnected idle: LED off
  if (!loggingEnabled) {
    ledOff();
    return;
  }
  
// Logging state
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
      sampleOverflow  = false;
      lastSampleTime  = 0;
      logStart_ms     = millis();
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

// Extended, line-oriented commands received over the USB serial port.
// Currently used for sample-rate configuration (R10/R25/R50).
static void processExtendedSerialCommand(const String &line) {
  if (line.length() < 2) {
    return;
  }

  char cmd = toupper(static_cast<unsigned char>(line[0]));

  switch (cmd) {
    case 'R': {  // R10 / R25 / R50
      uint16_t requested = 0;
      for (int i = 1; i < line.length(); ++i) {
        char ch = line[i];
        if (ch < '0' || ch > '9') {
          break;
        }
        requested = static_cast<uint16_t>(requested * 10u + static_cast<uint16_t>(ch - '0'));
      }
      if (requested == 0) {
        Serial.print(F("Invalid R command over Serial: '"));
        Serial.print(line);
        Serial.println('\'');
        return;
      }

      setSampleRateHz(requested);

      // Echo current configuration so the desktop app can update its UI
      Serial.print(F("CONFIG:sampleRateHz="));
      Serial.println(g_sampleRateHz);
      break;
    }

    default:
      // For now, just log unrecognised extended commands so the
      // protocol can be expanded later without silently ignoring input.
      Serial.print(F("RX LINE: "));
      Serial.println(line);
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

  if (flashStorage.begin()) {
    // Restore last configured sample rate (if any) so the device
    // comes up with the same rate the desktop app previously set.
    loadSampleRateFromFlash();
  } else {
    Serial.println(F("Flash storage init failed"));
  }
}

void loop() {
  unsigned long now = millis();

  // --- Button handling (logging / clear) ---
  updateButton(now);

  // --- Heartbeat on serial every few seconds ---
  static unsigned long lastHeartbeat = 0;
  if (now - lastHeartbeat > 5000) {
    lastHeartbeat = now;
    // Emit a simple heartbeat token that the desktop app can parse
    // to verify the link is still alive.
    Serial.println(F("DEVICE_ALIVE"));
  }

  // --- Serial commands (line-oriented) ---
  static String cmdLine;
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (millis() < ignoreSerialUntil) {
      continue; // ignore startup junk
    }

    // Treat CR/LF as end-of-command markers.
    if (c == '\r' || c == '\n') {
      if (cmdLine.length() == 0) {
        continue;
      }

      // Single-character commands (A,B,S,C,D,H) are handled by the
      // existing processSerialCommand() so the protocol stays the same.
      if (cmdLine.length() == 1) {
        char cmd = cmdLine[0];
        Serial.print(F("RX CMD: "));
        Serial.println(cmd);
        processSerialCommand(cmd);
      } else {
        // Multi-character, line-oriented commands such as sample-rate
        // configuration (R10/R25/R50) are handled here.
        processExtendedSerialCommand(cmdLine);
      }

      cmdLine = "";
      continue;
    }

    // Accumulate characters until we see a newline.
    cmdLine += c;
  }

  // --- Logging ---
  if (loggingEnabled) {
    // If flash storage has reached its defined capacity, stop logging
    // automatically and raise an overflow flag so the LED can show a
    // distinct error state.
    if (flashStorage.isFull()) {
      if (!sampleOverflow) {
        sampleOverflow  = true;
        loggingEnabled  = false;
        Serial.println(F("SAMPLE_CAPACITY_REACHED"));
      }
    } else if (now - lastSampleTime >= g_sampleIntervalMs) {
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

  // --- LED status ---
  updateStatusLED(now);
}
