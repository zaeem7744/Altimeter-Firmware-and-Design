#pragma once

#include <Arduino.h>
#include <FlashIAP.h>
#include <mbed.h>

using namespace mbed;

// Simple flash logger configuration
// NOTE: FLASH_MAGIC is used to distinguish this firmware's layout from any previous ones.
// If you ever change SensorSample layout again, bump FLASH_MAGIC to a new value.
#define FLASH_MAGIC         0x52425831  // 'RBX1' - Rocket Blackbox v1
#define SECTORS_COUNT       96
#define SAMPLES_PER_SECTOR  256
#define TOTAL_SAMPLES       (SECTORS_COUNT * SAMPLES_PER_SECTOR)
#define SAMPLE_RATE_HZ      50
#define SAMPLE_INTERVAL_MS  (1000 / SAMPLE_RATE_HZ)

struct SensorSample {
  float    time_s;     // seconds since logging started
  float    altitude_m; // barometric altitude in meters (as reported by BMP390)
  float    ax_ms2;     // accelerometer X axis (m/s^2)
  float    ay_ms2;     // accelerometer Y axis (m/s^2)
  float    az_ms2;     // accelerometer Z axis (m/s^2)
};

struct SectorData {
  uint32_t magic;          // 'ROCK'
  uint32_t sectorSequence;
  uint16_t samplesCount;
  uint16_t reserved;
  SensorSample samples[SAMPLES_PER_SECTOR];
};

class FlashStorage {
public:
  FlashStorage();

  bool begin();

  // Store a single sample: time in seconds from logging start, altitude, and 3-axis acceleration
  void addSample(float time_s, float altitude_m, float ax_ms2, float ay_ms2, float az_ms2);
  SensorSample getSampleAtIndex(uint32_t index);

  void clearStorage();
  void printStatus();

  uint32_t getTotalSamples() const { return totalSamplesRecorded; }
  uint32_t getMaxCapacity() const { return TOTAL_SAMPLES; }

  // Dump all stored samples as CSV to Serial: time_s,alt_m,acc_ms2
  void dumpToSerialSeconds();

private:
  FlashIAP   flash;
  SectorData currentSector;
  uint32_t   currentSectorIndex;
  uint32_t   totalSamplesRecorded;
  uint32_t   sequenceNumber;
  uint32_t   storageBaseAddr;
  uint32_t   sectorSize;

  SensorSample ramBuffer[10];
  uint8_t      ramBufferCount;

  void findLatestSector();
  void saveCurrentSector();
  void advanceToNextSector();
};

extern FlashStorage flashStorage;
