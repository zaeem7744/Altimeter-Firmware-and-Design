#include "FlashStorage.h"

FlashStorage flashStorage;

FlashStorage::FlashStorage() {
  currentSectorIndex   = 0;
  totalSamplesRecorded = 0;
  sequenceNumber       = 0;
  ramBufferCount       = 0;
}

bool FlashStorage::begin() {
  if (flash.init() != 0) {
    if (Serial) Serial.println("❌ FlashIAP init failed");
    return false;
  }

  uint32_t flashStart = flash.get_flash_start();
  sectorSize          = flash.get_sector_size(flashStart);
  uint32_t flashSize  = flash.get_flash_size();

  // Reserve lower 512 KB for program/bootloader, use upper 512 KB for data
  // This mirrors the working data_logger_flash_180_per_sector.cpp layout.
  uint32_t programRegionSize = 512 * 1024;
  if (flashSize <= programRegionSize) {
    if (Serial) Serial.println("❌ Not enough flash for data region");
    return false;
  }

  storageBaseAddr = flashStart + programRegionSize;
  uint32_t dataRegionSize = SECTORS_COUNT * sectorSize;
  if (storageBaseAddr + dataRegionSize > flashStart + flashSize) {
    if (Serial) Serial.println("❌ Data region exceeds available flash");
    return false;
  }

  if (Serial) {
    Serial.println("✅ Flash Storage Initialized (upper 512KB region)");
    Serial.print("Flash start: 0x"); Serial.println(flashStart, HEX);
    Serial.print("Flash size: "); Serial.print(flashSize / 1024); Serial.println(" KB");
    Serial.print("Storage base: 0x"); Serial.println(storageBaseAddr, HEX);
    Serial.print("Sector size: "); Serial.print(sectorSize); Serial.println(" bytes");
    Serial.print("Sectors: "); Serial.println(SECTORS_COUNT);
    Serial.print("Samples/sector: "); Serial.println(SAMPLES_PER_SECTOR);
    Serial.print("Total capacity: "); Serial.print(TOTAL_SAMPLES); Serial.println(" samples");
  }

  findLatestSector();
  return true;
}

void FlashStorage::findLatestSector() {
  uint32_t maxSequence  = 0;
  uint32_t latestSector = 0;
  totalSamplesRecorded  = 0;

  for (uint32_t i = 0; i < SECTORS_COUNT; ++i) {
    SectorData temp;
    uint32_t addr = storageBaseAddr + (i * sectorSize);
    if (flash.read(&temp, addr, sizeof(SectorData)) != 0) continue;

    if (temp.magic == FLASH_MAGIC && temp.sectorSequence > maxSequence) {
      maxSequence          = temp.sectorSequence;
      latestSector         = i;
      totalSamplesRecorded = i * SAMPLES_PER_SECTOR + temp.samplesCount;
    }
  }

  if (maxSequence > 0) {
    currentSectorIndex = latestSector;
    sequenceNumber     = maxSequence;
    uint32_t addr      = storageBaseAddr + (currentSectorIndex * sectorSize);
    flash.read(&currentSector, addr, sizeof(SectorData));
    if (Serial) {
      Serial.print("📁 Resumed storage, samples=");
      Serial.println(totalSamplesRecorded);
    }
  } else {
    currentSectorIndex           = 0;
    sequenceNumber               = 1;
    currentSector.magic          = FLASH_MAGIC;
    currentSector.sectorSequence = sequenceNumber;
    currentSector.samplesCount   = 0;
    if (Serial) Serial.println("🆕 Starting new storage");
  }
}

void FlashStorage::saveCurrentSector() {
  uint32_t sectorAddr = storageBaseAddr + (currentSectorIndex * sectorSize);
  currentSector.sectorSequence = sequenceNumber;

  if (flash.erase(sectorAddr, sectorSize) != 0) {
    if (Serial) Serial.println("❌ erase failed");
    return;
  }

  if (flash.program(&currentSector, sectorAddr, sizeof(SectorData)) != 0) {
    if (Serial) Serial.println("❌ program failed");
    return;
  }
}

void FlashStorage::advanceToNextSector() {
  currentSectorIndex = (currentSectorIndex + 1) % SECTORS_COUNT;
  sequenceNumber++;
  currentSector.samplesCount = 0;
  saveCurrentSector();
}

void FlashStorage::addSample(float time_s, float altitude_m,
                             float ax_ms2, float ay_ms2, float az_ms2) {
  if (totalSamplesRecorded >= TOTAL_SAMPLES) return;

  ramBuffer[ramBufferCount] = { time_s, altitude_m, ax_ms2, ay_ms2, az_ms2 };
  ramBufferCount++;

  if (ramBufferCount >= 10) {
    for (uint8_t i = 0; i < ramBufferCount; ++i) {
      if (currentSector.samplesCount >= SAMPLES_PER_SECTOR) {
        saveCurrentSector();
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

void FlashStorage::clearStorage() {
  if (Serial) Serial.println("🗑️ Clearing storage...");

  for (uint32_t i = 0; i < SECTORS_COUNT; ++i) {
    uint32_t sectorAddr = storageBaseAddr + (i * sectorSize);
    flash.erase(sectorAddr, sectorSize);
  }

  currentSectorIndex           = 0;
  totalSamplesRecorded         = 0;
  sequenceNumber               = 1;
  currentSector.magic          = FLASH_MAGIC;
  currentSector.sectorSequence = sequenceNumber;
  currentSector.samplesCount   = 0;
  saveCurrentSector();
}

void FlashStorage::printStatus() {
  Serial.println("=== FLASH STATUS ===");
  Serial.print("Samples: ");
  Serial.println(totalSamplesRecorded);
  Serial.print("Capacity: ");
  Serial.println(TOTAL_SAMPLES);
}

void FlashStorage::dumpToSerialSeconds() {
  if (totalSamplesRecorded == 0) {
    Serial.println("📭 No data in flash");
    return;
  }

  Serial.println("time_s,alt_m,ax_ms2,ay_ms2,az_ms2");

  struct SectorHeader {
    uint32_t index;
    uint32_t seq;
    uint16_t count;
  };

  SectorHeader headers[SECTORS_COUNT];
  uint8_t headerCount = 0;

  for (uint32_t i = 0; i < SECTORS_COUNT; ++i) {
    SectorData temp;
    uint32_t addr = storageBaseAddr + (i * sectorSize);
    if (flash.read(&temp, addr, sizeof(SectorData)) != 0) continue;

    if (temp.magic != FLASH_MAGIC || temp.samplesCount == 0) continue;

    uint8_t insertPos = headerCount;
    while (insertPos > 0 && headers[insertPos - 1].seq > temp.sectorSequence) {
      headers[insertPos] = headers[insertPos - 1];
      insertPos--;
    }
    headers[insertPos].index = i;
    headers[insertPos].seq   = temp.sectorSequence;
    headers[insertPos].count = temp.samplesCount;
    if (headerCount < SECTORS_COUNT) headerCount++;
  }

  uint32_t printed = 0;
  for (uint8_t h = 0; h < headerCount; ++h) {
    uint32_t sectorIdx = headers[h].index;
    uint16_t count     = headers[h].count;

    SectorData sector;
    uint32_t addr = storageBaseAddr + (sectorIdx * sectorSize);
    if (flash.read(&sector, addr, sizeof(SectorData)) != 0) continue;
    if (sector.magic != FLASH_MAGIC) continue;

    for (uint16_t sIdx = 0; sIdx < count; ++sIdx) {
      if (printed >= TOTAL_SAMPLES) break;
      SensorSample &s = sector.samples[sIdx];

      if (s.time_s == 0.0f && s.altitude_m == 0.0f &&
          s.ax_ms2 == 0.0f && s.ay_ms2 == 0.0f && s.az_ms2 == 0.0f) {
        continue;
      }

      Serial.print(s.time_s, 3); Serial.print(',');
      Serial.print(s.altitude_m, 2); Serial.print(',');
      Serial.print(s.ax_ms2, 3);    Serial.print(',');
      Serial.print(s.ay_ms2, 3);    Serial.print(',');
      Serial.println(s.az_ms2, 3);

      printed++;
      if (printed % 100 == 0) delay(5);
    }
  }

  Serial.println("=== END FLASH DUMP ===");
}
