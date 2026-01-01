#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_BMP3XX.h>
#include "FlashStorage.h"

#define SEALEVELPRESSURE_HPA (1013.25)

// IMU + baro objects
Adafruit_LSM6DSO32 lsm6;
Adafruit_BMP3XX    bmp;

// Logging control
bool loggingEnabled       = false;
unsigned long lastSampleTime = 0;
unsigned long logStart_ms    = 0;   // time zero for this logging session
unsigned long ignoreSerialUntil = 0;  // ignore junk for first 1 second

// Sample rate (50 samples per second)
const uint16_t SAMPLE_RATE_HZ     = 50;
const uint16_t SAMPLE_INTERVAL_MS = 1000 / SAMPLE_RATE_HZ;

void printHelp() {
  Serial.println();
  Serial.println(F("=== Altimeter Logger (FLASH + Serial) ==="));
  Serial.println(F("Commands (1 char + Enter):"));
  Serial.println(F("  A -> START logging to flash (50 samples/s)"));
  Serial.println(F("  B -> STOP logging"));
  Serial.println(F("  S -> STATUS (shows flash usage)"));
  Serial.println(F("  C -> CLEAR flash"));
  Serial.println(F("  D -> DUMP all stored samples"));
  Serial.println(F("  H -> HELP"));
  Serial.println();
  Serial.println(F("Dump format (D/E): time_s,alt_m,ax_ms2,ay_ms2,az_ms2"));
  Serial.println();
}

void printStatus() {
  Serial.println(F("=== STATUS ==="));
  Serial.print(F("Logging: "));
  Serial.println(loggingEnabled ? F("ON") : F("OFF"));
  flashStorage.printStatus();
}

// Initialize sensors
bool initSensors() {
  Wire.begin();
  Wire.setClock(400000);

  if (!lsm6.begin_I2C(0x6A, &Wire)) {
    Serial.println(F("❌ LSM6DSO32 not found"));
    return false;
  }
  lsm6.setAccelRange(LSM6DSO32_ACCEL_RANGE_8_G);
  lsm6.setAccelDataRate(LSM6DS_RATE_104_HZ);

  if (!bmp.begin_I2C(0x77, &Wire)) {
    Serial.println(F("❌ BMP390 not found"));
    return false;
  }
  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP3_ODR_50_HZ);

  Serial.println(F("✅ Sensors initialized"));
  return true;
}

// Handle single-character serial commands
void processSerialCommand(char c) {
  c = toupper(static_cast<unsigned char>(c));

  switch (c) {
    case 'A': {
      loggingEnabled = true;
      lastSampleTime = 0;
      logStart_ms    = millis();
      Serial.println(F("🟢 Logging to FLASH STARTED"));
      break;
    }

    case 'B':
      loggingEnabled = false;
      Serial.println(F("🔴 Logging STOPPED"));
      break;

    case 'S':
      printStatus();
      break;

    case 'C':
      flashStorage.clearStorage();
      break;

    case 'D':
      flashStorage.dumpToSerialSeconds();
      break;

    case 'H':
      printHelp();
      break;

    default:
      Serial.print(F("❓ Unknown command: "));
      Serial.println(c);
      Serial.println(F("Use A=START, B=STOP, S=STATUS, H=HELP"));
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);  // let USB settle

  // Ignore any garbage for the first 1 second after reset
  ignoreSerialUntil = millis() + 1000;

  Serial.println();
  Serial.println(F("=== Altimeter Logger (PlatformIO, FLASH + Serial) ==="));
  printHelp();

  if (!initSensors()) {
    Serial.println(F("⚠️ Sensor init failed. Logging disabled."));
  }

  flashStorage.begin();
}

void loop() {
  unsigned long now = millis();

  // Heartbeat every second
  static unsigned long lastHeartbeat = 0;
  if (now - lastHeartbeat > 1000) {
    lastHeartbeat = now;
    Serial.println(F("[HB] loop alive"));
  }

  // Handle incoming serial characters
  while (Serial.available() > 0) {
    char c = Serial.read();

    // Drop anything that arrives in the first second after reset
    if (millis() < ignoreSerialUntil) {
      continue;
    }

    if (c == '\r' || c == '\n') {
      // ignore CR/LF
      continue;
    }

    Serial.print(F("RX CMD: "));
    Serial.println(c);
    processSerialCommand(c);
  }

  // Periodic logging when enabled
  if (loggingEnabled) {
    if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
      lastSampleTime = now;

      sensors_event_t accel, gyro, temp;
      lsm6.getEvent(&accel, &gyro, &temp);

      if (!bmp.performReading()) {
        Serial.println(F("⚠️ BMP390 read failed"));
      } else {
        float altitude = bmp.readAltitude(SEALEVELPRESSURE_HPA);  // barometric altitude
        float ax       = accel.acceleration.x;
        float ay       = accel.acceleration.y;
        float az       = accel.acceleration.z;

        // Time in seconds since START was issued
        float time_s = (now - logStart_ms) / 1000.0f;

        // Store into FLASH (persistent across power cycles)
        flashStorage.addSample(time_s, altitude, ax, ay, az);
        // No live CSV streaming; use D/E to dump when needed
      }
    }
  }
}
