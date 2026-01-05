#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Wire.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_BMP3XX.h>
#include "FlashStorage.h"

#define SEALEVELPRESSURE_HPA (1013.25)

// Global flag used to temporarily suppress non-essential activity
// (like serial heartbeats) during large BLE exports.
bool g_exportInProgress = false;

// Monotonic session identifier used for FILEINFO/FGET-style exports so
// the host can distinguish separate export attempts if desired.
static uint32_t g_fileExportSessionId = 0;

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
  Serial.println(F("  A -> START logging to flash (current sample rate)"));
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

// ---------------- BLE helpers ----------------

// Small helper for one-off status lines over BLE (MEMORY:, CONFIG:, etc.).
// This sends a single line per notification and is NOT used for large dumps.
void bleSendLine(const char* line, void* /*ctx*/) {
  if (!bleInitialized) return;
  if (!line || !*line) return;

  // Keep payload comfortably under the 64-byte characteristic size.
  char buf[64];
  size_t len = strlen(line);
  if (len > sizeof(buf) - 2) {
    len = sizeof(buf) - 2;
  }
  memcpy(buf, line, len);
  buf[len]     = '\n';
  buf[len + 1] = '\0';

  bleTxChar.writeValue((const uint8_t*)buf, len + 1);
  delay(2); // tiny pacing for infrequent lines
}

// For large flash exports we batch multiple CSV lines into each
// notification to reduce the total number of BLE packets. The
// desktop app parses the stream by newlines, so it is agnostic
// to how many logical lines arrive per notification.
static char   g_bleDumpBuf[64];
static size_t g_bleDumpLen = 0;

static void bleFlushDumpBuffer() {
  if (!bleInitialized || g_bleDumpLen == 0) {
    return;
  }
  bleTxChar.writeValue((const uint8_t*)g_bleDumpBuf, g_bleDumpLen);
  g_bleDumpLen = 0;
  // Maintain a small delay so we do not completely saturate the link.
  delay(2);
}

// Callback used only for flash dumps over BLE.
// It concatenates multiple CSV lines (separated by '\n') into a
// single notification up to ~60 bytes before flushing.
void bleDumpSendLine(const char* line, void* /*ctx*/) {
  if (!bleInitialized || !line || !*line) return;

  const size_t maxPayload = sizeof(g_bleDumpBuf) - 1; // leave room for final '\n'
  size_t lineLen = strlen(line);
  size_t pos     = 0;

  while (pos < lineLen) {
    // Flush if no space left for at least one more character.
    if (g_bleDumpLen >= maxPayload) {
      bleFlushDumpBuffer();
    }

    size_t space      = maxPayload - g_bleDumpLen;
    size_t chunkBytes = lineLen - pos;
    if (chunkBytes > space) {
      chunkBytes = space;
    }

    memcpy(&g_bleDumpBuf[g_bleDumpLen], &line[pos], chunkBytes);
    g_bleDumpLen += chunkBytes;
    pos          += chunkBytes;

    // If we filled the buffer exactly, flush and continue writing
    // remaining bytes (if any) in a new packet.
    if (g_bleDumpLen >= maxPayload) {
      bleFlushDumpBuffer();
    }
  }

  // Append newline as logical line terminator. If there is no room
  // for it in the current packet, flush first.
  if (g_bleDumpLen >= maxPayload) {
    bleFlushDumpBuffer();
  }
  g_bleDumpBuf[g_bleDumpLen++] = '\n';
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

// Forward-declare so BLE handlers can call them before full definition.
void processSerialCommand(char c);
void stopBle();

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
      // Status: print to Serial AND send a compact MEMORY + CONFIG line over BLE
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

      snprintf(
        buf,
        sizeof(buf),
        "CONFIG:sampleRateHz=%u",
        (unsigned)g_sampleRateHz
      );
      bleSendLine(buf, nullptr);
      break;
    }

    case 'R': {
      // Set sample rate from BLE: expect ASCII digits after 'R', e.g. "R10"/"R25"/"R50"
      uint16_t requested = 0;
      for (int i = 1; i < len; ++i) {
        char ch = (char)data[i];
        if (ch < '0' || ch > '9') break;
        requested = (uint16_t)(requested * 10u + (uint16_t)(ch - '0'));
      }
      if (requested == 0) {
        Serial.println(F("Invalid R command (no digits)"));
        break;
      }
      setSampleRateHz(requested);

      char buf[48];
      snprintf(
        buf,
        sizeof(buf),
        "CONFIG:sampleRateHz=%u",
        (unsigned)g_sampleRateHz
      );
      bleSendLine(buf, nullptr);
      break;
    }

    case 'B':
      // Stop logging AND explicitly shut down BLE when requested over BLE.
      processSerialCommand(cmd);
      stopBle();
      break;

    case 'A':
    case 'C':
    case 'H':
      // Reuse existing command handler so LED + logging behaviour stay consistent.
      processSerialCommand(cmd);
      break;

    case 'D':
      // Dump flash contents over BLE (CSV via TX characteristic)
      Serial.println(F("BLE RX: 'D' received, dumping flash over BLE"));
      flashStorage.dumpToCallback(bleDumpSendLine, nullptr);
      // Flush any remaining batched data after the dump completes.
      bleFlushDumpBuffer();
      break;

    case 'F': {
      // File‑style chunk transfer commands: FINFO or FGET:<index>
      // Parse payload after 'F' to distinguish subcommands.
      if (len >= 5 && data[1] == 'I' && data[2] == 'N' && data[3] == 'F' && data[4] == 'O') {
        // FINFO: return file metadata (total samples, chunk size, etc.)
        Serial.println(F("BLE RX: FINFO received"));
        uint32_t total = flashStorage.getTotalSamples();
        const uint32_t samplesPerChunk = 100; // smaller chunks for more reliable long transfers
        uint32_t totalChunks = (total + samplesPerChunk - 1) / samplesPerChunk;

        // Bump session id so each FILEINFO represents a distinct snapshot
        // of the flash contents.
        ++g_fileExportSessionId;
        if (g_fileExportSessionId == 0) {
          g_fileExportSessionId = 1; // avoid zero as a valid session
        }

        char buf[96];
        snprintf(
          buf,
          sizeof(buf),
          "FILEINFO:totalSamples=%lu,samplesPerChunk=%lu,totalChunks=%lu,sessionId=%lu",
          (unsigned long)total,
          (unsigned long)samplesPerChunk,
          (unsigned long)totalChunks,
          (unsigned long)g_fileExportSessionId
        );
        bleSendLine(buf, nullptr);
        Serial.println(buf);
      } else if (len >= 4 && data[1] == 'G' && data[2] == 'E' && data[3] == 'T') {
        // FGET:<chunkIndex> – parse ASCII digits after "FGET:"
        uint32_t chunkIdx = 0;
        bool valid = false;
        // Look for the colon separator
        int colonPos = -1;
        for (int i = 4; i < len; ++i) {
          if ((char)data[i] == ':') {
            colonPos = i;
            break;
          }
        }
        if (colonPos >= 0) {
          for (int i = colonPos + 1; i < len; ++i) {
            char ch = (char)data[i];
            if (ch >= '0' && ch <= '9') {
              chunkIdx = chunkIdx * 10u + (uint32_t)(ch - '0');
              valid = true;
            } else {
              break;
            }
          }
        }

        if (valid) {
          Serial.print(F("BLE RX: FGET chunk="));
          Serial.println(chunkIdx);
          const uint32_t samplesPerChunk = 100; // must match FINFO
          g_exportInProgress = true;
          flashStorage.dumpChunkToCallback(chunkIdx, samplesPerChunk, bleDumpSendLine, nullptr);
          g_exportInProgress = false;
          bleFlushDumpBuffer();
        } else {
          Serial.println(F("BLE RX: FGET invalid chunk index"));
          bleSendLine("FERROR:INVALID_CHUNK", nullptr);
        }
      } else {
        Serial.println(F("BLE RX: unknown F command"));
      }
      break;
    }

    default:
      // Ignore other bytes; command set remains A,B,S,C,D,H,F,R.
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

// Helper used by FlashStorage during long dumps to keep the BLE
// connection alive. Called once per sample from dumpToCallback().
void bleYield() {
  if (bleInitialized && (bleAdvertising || bleConnected)) {
    BLE.poll();
  }
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

  // --- Button handling (logging / BLE / clear) ---
  updateButton(now);

  // --- Heartbeat on serial every few seconds ---
  static unsigned long lastHeartbeat = 0;
  if (!g_exportInProgress && (now - lastHeartbeat > 5000)) {
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
        // Multi-character commands (e.g. future extensions like FINFO)
        // are logged for now so the host can evolve without breaking
        // backwards compatibility.
        Serial.print(F("RX LINE: "));
        Serial.println(cmdLine);
        // TODO: parse extended text commands here if needed.
      }

      cmdLine = "";
      continue;
    }

    // Accumulate characters until we see a newline.
    cmdLine += c;
  }

  // --- Logging ---
  if (loggingEnabled) {
    if (now - lastSampleTime >= g_sampleIntervalMs) {
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
