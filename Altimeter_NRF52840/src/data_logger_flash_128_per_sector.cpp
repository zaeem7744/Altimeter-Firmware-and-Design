/*
  Project: Altimeter_NRF52840 — Rocket Telemetry Firmware (Sample)
  File: utilities/firmware_samples/data_logger_flash_128_per_sector.cpp
  Purpose: Log gyroscope data to internal flash with 128 samples/sector configuration.
  Inputs: Serial commands: 'A' start, 'B' stop, 'S' status, 'E' export CSV, 'C' clear.
  Outputs: Writes to internal flash; prints status and CSV export to Serial.
  Notes: Intended as a standalone sketch/sample; not built as part of main firmware.
*/
#include <Arduino.h>
#include <Arduino_BMI270_BMM150.h>
#include <FlashIAP.h>
#include <mbed.h>

using namespace mbed;

struct GyroSample {
  int16_t gx, gy, gz;
  uint32_t timestamp;
};

#define SECTORS_COUNT 96
#define SAMPLES_PER_SECTOR 128
#define TOTAL_SAMPLES (SECTORS_COUNT * SAMPLES_PER_SECTOR)

struct SectorData {
  uint32_t magic;
  uint32_t sectorSequence;
  uint16_t samplesCount;
  uint16_t reserved;
  GyroSample samples[SAMPLES_PER_SECTOR];
};

SectorData currentSector;
uint32_t currentSectorIndex = 0;
uint32_t totalSamplesRecorded = 0;
uint32_t sequenceNumber = 0;
GyroSample ramBuffer[20];
uint8_t ramBufferCount = 0;
bool logging = false;
unsigned long lastSampleTime = 0;
FlashIAP flash;
uint32_t storageBaseAddr;

void findLatestSector();
void saveCurrentSector();
void advanceToNextSector();
void addSample(float gx, float gy, float gz);
void exportAllData();
void printStatus();
void clearStorage();

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("ROCKET TELEMETRY - MAXIMIZED STORAGE");
  Serial.println("=====================================");
  
  if (flash.init() != 0) {
    Serial.println("FlashIAP init failed");
    return;
  }
  
  Serial.println("FlashIAP initialized");
  
  uint32_t flashStart = flash.get_flash_start();
  uint32_t flashSize = flash.get_flash_size();
  uint32_t sectorSize = flash.get_sector_size(flashStart);
  
  storageBaseAddr = flashStart + flashSize - (SECTORS_COUNT * sectorSize);
  
  Serial.print("Sectors: ");
  Serial.println(SECTORS_COUNT);
  Serial.print("Samples per sector: ");
  Serial.println(SAMPLES_PER_SECTOR);
  Serial.print("Total capacity: ");
  Serial.print(TOTAL_SAMPLES);
  Serial.println(" samples");
  Serial.print("Flight duration: ");
  Serial.print(TOTAL_SAMPLES / 50);
  Serial.println(" seconds");
  
  findLatestSector();
  
  if (!IMU.begin()) {
    Serial.println("IMU initialization failed");
    while(1);
  }
  Serial.println("IMU ready");
  
  Serial.print("Current state: Sector ");
  Serial.print(currentSectorIndex);
  Serial.print(", Samples: ");
  Serial.println(totalSamplesRecorded);
  Serial.println("Commands: A=Start, B=Stop, S=Status, E=Export, C=Clear");
}

void findLatestSector() {
  uint32_t maxSequence = 0;
  uint32_t latestSector = 0;
  totalSamplesRecorded = 0;
  
  for (uint32_t i = 0; i < SECTORS_COUNT; i++) {
    SectorData temp;
    uint32_t addr = storageBaseAddr + (i * flash.get_sector_size(storageBaseAddr));
    flash.read(&temp, addr, sizeof(SectorData));
    
    if (temp.magic == 0x524F434B && temp.sectorSequence > maxSequence) {
      maxSequence = temp.sectorSequence;
      latestSector = i;
      totalSamplesRecorded = i * SAMPLES_PER_SECTOR + temp.samplesCount;
    }
  }
  
  if (maxSequence > 0) {
    currentSectorIndex = latestSector;
    sequenceNumber = maxSequence;
    uint32_t addr = storageBaseAddr + (currentSectorIndex * flash.get_sector_size(storageBaseAddr));
    flash.read(&currentSector, addr, sizeof(SectorData));
    Serial.println("Resumed from existing data");
  } else {
    currentSectorIndex = 0;
    sequenceNumber = 1;
    currentSector.magic = 0x524F434B;
    currentSector.sectorSequence = sequenceNumber;
    currentSector.samplesCount = 0;
    Serial.println("Starting fresh storage");
  }
}

void saveCurrentSector() {
  uint32_t sectorAddr = storageBaseAddr + (currentSectorIndex * flash.get_sector_size(storageBaseAddr));
  
  currentSector.sectorSequence = sequenceNumber;
  
  flash.erase(sectorAddr, flash.get_sector_size(storageBaseAddr));
  flash.program(&currentSector, sectorAddr, sizeof(SectorData));
  
  Serial.print("Saved sector ");
  Serial.print(currentSectorIndex);
  Serial.print(" | Samples: ");
  Serial.print(currentSector.samplesCount);
  Serial.print(" | Total: ");
  Serial.println(totalSamplesRecorded);
}

void advanceToNextSector() {
  currentSectorIndex = (currentSectorIndex + 1) % SECTORS_COUNT;
  sequenceNumber++;
  currentSector.samplesCount = 0;
  
  Serial.print("Advanced to sector ");
  Serial.println(currentSectorIndex);
  
  saveCurrentSector();
}

void addSample(float gx, float gy, float gz) {
  ramBuffer[ramBufferCount] = {
    (int16_t)(gx * 100),
    (int16_t)(gy * 100),
    (int16_t)(gz * 100),
    millis()
  };
  ramBufferCount++;
  
  if (ramBufferCount >= 10) {
    for (int i = 0; i < ramBufferCount; i++) {
      if (currentSector.samplesCount >= SAMPLES_PER_SECTOR) {
        advanceToNextSector();
      }
      
      currentSector.samples[currentSector.samplesCount] = ramBuffer[i];
      currentSector.samplesCount++;
      totalSamplesRecorded++;
    }
    
    saveCurrentSector();
    ramBufferCount = 0;
  }
}

void exportAllData() {
  if (totalSamplesRecorded == 0) {
    Serial.println("No data to export");
    return;
  }
  
  Serial.println("BEGIN_DATA_EXPORT");
  Serial.println("timestamp,gyro_x,gyro_y,gyro_z");
  
  for (uint32_t i = 0; i < totalSamplesRecorded && i < TOTAL_SAMPLES; i++) {
    uint32_t sectorIdx = i / SAMPLES_PER_SECTOR;
    uint32_t sampleIdx = i % SAMPLES_PER_SECTOR;
    
    SectorData sector;
    uint32_t addr = storageBaseAddr + (sectorIdx * flash.get_sector_size(storageBaseAddr));
    flash.read(&sector, addr, sizeof(SectorData));
    
    if (sector.magic == 0x524F434B && sampleIdx < sector.samplesCount) {
      Serial.print(sector.samples[sampleIdx].timestamp);
      Serial.print(",");
      Serial.print(sector.samples[sampleIdx].gx / 100.0, 4);
      Serial.print(",");
      Serial.print(sector.samples[sampleIdx].gy / 100.0, 4);
      Serial.print(",");
      Serial.print(sector.samples[sampleIdx].gz / 100.0, 4);
      Serial.println();
    }
    
    if (i % 50 == 0) delay(10);
  }
  
  Serial.println("END_DATA_EXPORT");
  Serial.print("Exported ");
  Serial.print(min(totalSamplesRecorded, TOTAL_SAMPLES));
  Serial.println(" samples");
}

void printStatus() {
  Serial.println("=== STORAGE STATUS ===");
  Serial.print("Total samples recorded: ");
  Serial.println(totalSamplesRecorded);
  Serial.print("Current sector: ");
  Serial.println(currentSectorIndex);
  Serial.print("Samples in current sector: ");
  Serial.print(currentSector.samplesCount);
  Serial.print("/");
  Serial.println(SAMPLES_PER_SECTOR);
  Serial.print("Overall usage: ");
  Serial.print((min(totalSamplesRecorded, TOTAL_SAMPLES) * 100) / TOTAL_SAMPLES);
  Serial.println("%");
  Serial.print("Max capacity: ");
  Serial.print(TOTAL_SAMPLES);
  Serial.println(" samples");
  Serial.print("Logging: ");
  Serial.println(logging ? "ACTIVE" : "INACTIVE");
  Serial.println("======================");
}

void clearStorage() {
  Serial.println("Clearing all storage sectors...");
  
  currentSectorIndex = 0;
  totalSamplesRecorded = 0;
  currentSector.samplesCount = 0;
  sequenceNumber = 1;
  currentSector.magic = 0x524F434B;
  currentSector.sectorSequence = sequenceNumber;
  
  for (uint32_t i = 0; i < SECTORS_COUNT; i++) {
    uint32_t sectorAddr = storageBaseAddr + (i * flash.get_sector_size(storageBaseAddr));
    flash.erase(sectorAddr, flash.get_sector_size(storageBaseAddr));
    
    SectorData emptySector = {0, 0, 0, 0, {}};
    flash.program(&emptySector, sectorAddr, sizeof(SectorData));
  }
  
  saveCurrentSector();
  
  Serial.println("Storage completely cleared");
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '\n' || cmd == '\r') return;
    
    switch(toupper(cmd)) {
      case 'A':
        logging = true;
        Serial.println("Logging started");
        break;
      case 'B':
        logging = false;
        if (ramBufferCount > 0) {
          for (int i = 0; i < ramBufferCount; i++) {
            addSample(
              ramBuffer[i].gx / 100.0, 
              ramBuffer[i].gy / 100.0, 
              ramBuffer[i].gz / 100.0
            );
          }
          ramBufferCount = 0;
        }
        Serial.println("Logging stopped");
        break;
      case 'S': 
        printStatus(); 
        break;
      case 'E': 
        exportAllData(); 
        break;
      case 'C': 
        clearStorage();
        break;
    }
  }
  
  if (logging && (millis() - lastSampleTime >= 20)) {
    lastSampleTime = millis();
    if (IMU.gyroscopeAvailable()) {
      float gx, gy, gz;
      IMU.readGyroscope(gx, gy, gz);
      addSample(gx, gy, gz);
    }
  }
  
  delay(1);
}
