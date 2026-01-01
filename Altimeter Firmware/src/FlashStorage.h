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

  void clearStorage();
  void printStatus();
  void dumpToSerialSeconds();  // CSV: time_s,alt_m,ax_ms2,ay_ms2,az_ms2

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
