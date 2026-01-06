#pragma once

#include <Arduino.h>
#include <FlashIAP.h>
#include <mbed.h>

using namespace mbed;

// Flash logger configuration (based on working 180-per-sector example)
#define FLASH_MAGIC         0x52425831  // 'RBX1'
#define SECTORS_COUNT       96          // sectors reserved for data
#define SAMPLES_PER_SECTOR  180         // samples per sector
#define TOTAL_SAMPLES       (SECTORS_COUNT * SAMPLES_PER_SECTOR)

// Logged sample layout
struct SensorSample {
  float time_s;     // seconds since logging started
  float altitude_m; // barometric altitude in meters
  float ax_ms2;     // accelerometer X (m/s^2)
  float ay_ms2;     // accelerometer Y (m/s^2)
  float az_ms2;     // accelerometer Z (m/s^2)
};

struct SectorData {
  uint32_t magic;
  uint32_t sectorSequence;
  uint16_t samplesCount;
  uint16_t reserved;
  SensorSample samples[SAMPLES_PER_SECTOR];
};

class FlashStorage {
public:
  FlashStorage();

  bool begin();
  
  void addSample(float time_s, float altitude_m,
                 float ax_ms2, float ay_ms2, float az_ms2);

  // Returns true when the storage region is full (no more samples will
  // be accepted). Used by main.cpp to stop logging cleanly when the
  // defined capacity has been reached.
  bool isFull() const { return totalSamplesRecorded >= TOTAL_SAMPLES; }

  void clearStorage();
  void printStatus();
  void dumpToSerialSeconds();  // CSV: time_s,alt_m,ax_ms2,ay_ms2,az_ms2

  // Accessor for total *valid* samples that will actually be exported
  // over Serial. This scans the stored sectors and counts only
  // samples that pass isSampleValid(), so MEMORY lines match
  // the rows produced by dumpToSerialSeconds().
  uint32_t getTotalSamples();

  // Expose storage layout so other modules (e.g. config) can place data
  // in a reserved region outside the logging sectors.
  uint32_t getStorageBaseAddr() const { return storageBaseAddr; }
  uint32_t getSectorSize()     const { return sectorSize; }

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
