// flash_storage.h - COMPLETE
#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <Arduino.h>
#include <FlashIAP.h>
#include <mbed.h>

using namespace mbed;

// Storage configuration - OPTIMIZED
#define SECTORS_COUNT 96
#define SAMPLES_PER_SECTOR 256  // Increased for better efficiency
#define TOTAL_SAMPLES (SECTORS_COUNT * SAMPLES_PER_SECTOR)
#define SAMPLE_RATE_HZ 50
#define SAMPLE_INTERVAL_MS (1000 / SAMPLE_RATE_HZ)

// Data structures - SIMPLIFIED
struct SensorSample {
  uint32_t timestamp;
  float altitude;
  float acceleration;
  // Removed velocity and temperature to save space
};

struct SectorData {
  uint32_t magic;
  uint32_t sectorSequence;
  uint16_t samplesCount;
  uint16_t reserved;
  SensorSample samples[SAMPLES_PER_SECTOR];
};

class FlashStorage {
private:
  FlashIAP flash;
  SectorData currentSector;
  uint32_t currentSectorIndex;
  uint32_t totalSamplesRecorded;
  uint32_t sequenceNumber;
  uint32_t storageBaseAddr;
  uint32_t sectorSize;
  
  SensorSample ramBuffer[10];
  uint8_t ramBufferCount;
  unsigned long lastSampleTime;
  
  void findLatestSector();
  void saveCurrentSector();
  void advanceToNextSector();

public:
  FlashStorage();
  bool begin();
  void addSample(float altitude, float acceleration, uint32_t timestamp);
  SensorSample getSampleAtIndex(uint32_t index);
  void exportAllData();
  void printStatus();
  void clearStorage();
  uint32_t getTotalSamples();
  uint32_t getUsagePercent();
  bool isFull();
  uint32_t getMaxCapacity();
};

extern FlashStorage flashStorage;

#endif