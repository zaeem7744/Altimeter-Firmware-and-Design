// flash_storage.cpp - copied from Altimeter_NRF services for Bluetooth firmware
#include "flash_storage.h"

FlashStorage flashStorage;

FlashStorage::FlashStorage() {
  currentSectorIndex = 0;
  totalSamplesRecorded = 0;
  sequenceNumber = 0;
  ramBufferCount = 0;
  lastSampleTime = 0;
}

bool FlashStorage::begin() {
  if (flash.init() != 0) {
    if (Serial) Serial.println("❌ FlashIAP init failed");
    return false;
  }
  
  uint32_t flashStart = flash.get_flash_start();
  uint32_t flashSize = flash.get_flash_size();
  sectorSize = flash.get_sector_size(flashStart);
  
  // Reserve first 512KB for program, use upper 512KB for data
  storageBaseAddr = flashStart + (512 * 1024);
  
  if (Serial) {
    Serial.println("✅ Flash Storage Initialized");
    Serial.print("📦 Total capacity: ");
    Serial.print(TOTAL_SAMPLES);
    Serial.println(" samples");
    Serial.print("⏱️  Recording duration: ");
    Serial.print(TOTAL_SAMPLES / SAMPLE_RATE_HZ);
    Serial.println(" seconds");
  }
  
  findLatestSector();
  return true;
}

void FlashStorage::findLatestSector() {
  uint32_t maxSequence = 0;
  uint32_t latestSector = 0;
  totalSamplesRecorded = 0;
  
  for (uint32_t i = 0; i < SECTORS_COUNT; i++) {
    SectorData temp;
    uint32_t addr = storageBaseAddr + (i * sectorSize);
    if (flash.read(&temp, addr, sizeof(SectorData)) != 0) {
      continue;
    }
    
    if (temp.magic == 0x524F434B && temp.sectorSequence > maxSequence) {
      maxSequence = temp.sectorSequence;
      latestSector = i;
      totalSamplesRecorded = i * SAMPLES_PER_SECTOR + temp.samplesCount;
    }
  }
  
  if (maxSequence > 0) {
    currentSectorIndex = latestSector;
    sequenceNumber = maxSequence;
    uint32_t addr = storageBaseAddr + (currentSectorIndex * sectorSize);
    flash.read(&currentSector, addr, sizeof(SectorData));
    if (Serial) {
      Serial.print("📁 Resumed from existing data - ");
      Serial.print(totalSamplesRecorded);
      Serial.println(" samples");
    }
  } else {
    currentSectorIndex = 0;
    sequenceNumber = 1;
    currentSector.magic = 0x524F434B;
    currentSector.sectorSequence = sequenceNumber;
    currentSector.samplesCount = 0;
    if (Serial) Serial.println("🆕 Starting fresh storage");
  }
}

void FlashStorage::saveCurrentSector() {
  uint32_t sectorAddr = storageBaseAddr + (currentSectorIndex * sectorSize);
  currentSector.sectorSequence = sequenceNumber;
  
  if (Serial) {
    Serial.print("💾 Saving sector ");
    Serial.print(currentSectorIndex);
    Serial.print(" with ");
    Serial.print(currentSector.samplesCount);
    Serial.println(" samples");
  }
  
  if (flash.erase(sectorAddr, sectorSize) != 0) {
    if (Serial) Serial.println("❌ Error erasing sector");
    return;
  }
  
  if (flash.program(&currentSector, sectorAddr, sizeof(SectorData)) != 0) {
    if (Serial) Serial.println("❌ Error programming sector");
    return;
  }
  
  if (Serial) Serial.println("✅ Sector saved successfully");
}

void FlashStorage::advanceToNextSector() {
  currentSectorIndex = (currentSectorIndex + 1) % SECTORS_COUNT;
  sequenceNumber++;
  currentSector.samplesCount = 0;
  
  if (Serial) {
    Serial.print("➡️ Advanced to sector ");
    Serial.println(currentSectorIndex);
  }
  saveCurrentSector();
}

void FlashStorage::addSample(const FlightSample &sample) {
  // Add to RAM buffer
  ramBuffer[ramBufferCount] = sample;
  ramBufferCount++;
  
  // Optional debug output - only if serial is available and not too frequent
  static unsigned long lastDebugTime = 0;
  if (Serial && (millis() - lastDebugTime > 5000)) { // Debug every 5 seconds max
    Serial.print("📝 Sample buffer: Alt=");
    Serial.print(sample.alt_m);
    Serial.print(", Az=");
    Serial.print(sample.az_ms2);
    Serial.print(", Count=");
    Serial.println(ramBufferCount);
    lastDebugTime = millis();
  }
  
  // Write to flash when buffer is full
  if (ramBufferCount >= 10) {
    if (Serial) {
      Serial.print("💾 Writing ");
      Serial.print(ramBufferCount);
      Serial.println(" samples to flash");
    }
    
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

FlightSample FlashStorage::getSampleAtIndex(uint32_t index) {
  if (index >= totalSamplesRecorded) {
    FlightSample empty = {};
    return empty;
  }
  
  uint32_t sectorIdx = index / SAMPLES_PER_SECTOR;
  uint32_t sampleIdx = index % SAMPLES_PER_SECTOR;
  
  SectorData sector;
  uint32_t addr = storageBaseAddr + (sectorIdx * sectorSize);
  
  if (flash.read(&sector, addr, sizeof(SectorData)) != 0) {
    Serial.print("❌ Error reading sector ");
    Serial.println(sectorIdx);
    FlightSample empty = {};
    return empty;
  }
  
  if (sector.magic == 0x524F434B && sampleIdx < sector.samplesCount) {
    return sector.samples[sampleIdx];
  }
  
  FlightSample empty = {};
  return empty;
}

void FlashStorage::exportAllData() {
  if (totalSamplesRecorded == 0) {
    Serial.println("📭 No data to export");
    return;
  }
  
  Serial.println("BEGIN_DATA_EXPORT");
  Serial.println("t_ms,alt_m,ax_ms2,ay_ms2,az_ms2,gx_rad_s,gy_rad_s,gz_rad_s,temp_C");
  
  uint32_t samplesExported = 0;
  
  Serial.print("🔍 Exporting ");
  Serial.print(totalSamplesRecorded);
  Serial.println(" samples from flash...");
  
  for (uint32_t i = 0; i < totalSamplesRecorded && i < TOTAL_SAMPLES; i++) {
    uint32_t sectorIdx = i / SAMPLES_PER_SECTOR;
    uint32_t sampleIdx = i % SAMPLES_PER_SECTOR;
    
    SectorData sector;
    uint32_t addr = storageBaseAddr + (sectorIdx * sectorSize);
    
    if (flash.read(&sector, addr, sizeof(SectorData)) != 0) {
      Serial.print("❌ Error reading sector ");
      Serial.println(sectorIdx);
      continue;
    }
    
    if (sector.magic == 0x524F434B && sampleIdx < sector.samplesCount) {
      const FlightSample &s = sector.samples[sampleIdx];
      Serial.print(s.t_ms);
      Serial.print(",");
      Serial.print(s.alt_m, 2);
      Serial.print(",");
      Serial.print(s.ax_ms2, 2);
      Serial.print(",");
      Serial.print(s.ay_ms2, 2);
      Serial.print(",");
      Serial.print(s.az_ms2, 2);
      Serial.print(",");
      Serial.print(s.gx_rad_s, 3);
      Serial.print(",");
      Serial.print(s.gy_rad_s, 3);
      Serial.print(",");
      Serial.print(s.gz_rad_s, 3);
      Serial.print(",");
      Serial.print(s.temp_C, 2);
      Serial.println();
      samplesExported++;
      
      // Progress indicator
      if (samplesExported % 100 == 0) {
        Serial.print("📤 Exported ");
        Serial.print(samplesExported);
        Serial.print("/");
        Serial.print(totalSamplesRecorded);
        Serial.println(" samples");
      }
    }
    
    if (i % 50 == 0) delay(10); // Prevent serial buffer overflow
  }
  
  Serial.println("END_DATA_EXPORT");
  Serial.print("✅ Export complete: ");
  Serial.print(samplesExported);
  Serial.println(" samples exported");
  Serial.println("DATA_EXPORT_COMPLETE");
}

void FlashStorage::printStatus() {
  Serial.println("=== FLASH STORAGE STATUS ===");
  Serial.print("📊 Total samples: ");
  Serial.println(totalSamplesRecorded);
  Serial.print("💾 Usage: ");
  Serial.print(getUsagePercent());
  Serial.println("%");
  Serial.print("📁 Current sector: ");
  Serial.println(currentSectorIndex);
  Serial.print("🔢 Samples in sector: ");
  Serial.print(currentSector.samplesCount);
  Serial.print("/");
  Serial.println(SAMPLES_PER_SECTOR);
  Serial.print("⏱️  Recording time: ");
  Serial.print(totalSamplesRecorded / SAMPLE_RATE_HZ);
  Serial.println(" seconds");
  Serial.println("============================");
}

void FlashStorage::clearStorage() {
  Serial.println("🗑️ Clearing all storage sectors...");
  
  currentSectorIndex = 0;
  totalSamplesRecorded = 0;
  currentSector.samplesCount = 0;
  sequenceNumber = 1;
  currentSector.magic = 0x524F434B;
  currentSector.sectorSequence = sequenceNumber;
  
  // Erase all sectors
  for (uint32_t i = 0; i < SECTORS_COUNT; i++) {
    uint32_t sectorAddr = storageBaseAddr + (i * sectorSize);
    
    Serial.print("🗑️ Erasing sector ");
    Serial.println(i);
    
    if (flash.erase(sectorAddr, sectorSize) != 0) {
      Serial.print("❌ Error erasing sector ");
      Serial.println(i);
      continue;
    }
    
    SectorData emptySector = {0, 0, 0, 0, {}};
    if (flash.program(&emptySector, sectorAddr, sizeof(SectorData)) != 0) {
      Serial.print("❌ Error programming sector ");
      Serial.println(i);
    }
    
    if ((i + 1) % 16 == 0) {
      Serial.print(".");
    }
  }
  
  saveCurrentSector();
  Serial.println(" ✅ Storage cleared");
  Serial.println("MEMORY_CLEARED");
}

uint32_t FlashStorage::getTotalSamples() {
  return totalSamplesRecorded;
}

uint32_t FlashStorage::getUsagePercent() {
  return (min(totalSamplesRecorded, TOTAL_SAMPLES) * 100) / TOTAL_SAMPLES;
}

uint32_t FlashStorage::getMaxCapacity() {
  return TOTAL_SAMPLES;
}

bool FlashStorage::isFull() {
  return totalSamplesRecorded >= TOTAL_SAMPLES;
}

void FlashStorage::dumpToSerialSeconds() {
  if (totalSamplesRecorded == 0) {
    Serial.println("📭 No data in flash");
    return;
  }

  Serial.println("=== FLASH DUMP (time_s) ===");

  // Find first valid sample as time zero
  FlightSample first = getSampleAtIndex(0);
  uint32_t t0 = first.t_ms;

  Serial.println("time_s,alt_m,ax_ms2,ay_ms2,az_ms2,gx_rad_s,gy_rad_s,gz_rad_s,temp_C");

  for (uint32_t i = 0; i < totalSamplesRecorded && i < TOTAL_SAMPLES; ++i) {
    FlightSample s = getSampleAtIndex(i);

    // Skip obviously empty samples
    if (s.t_ms == 0 && s.alt_m == 0.0f && s.az_ms2 == 0.0f) {
      continue;
    }

    float time_s = (s.t_ms - t0) / 1000.0f;

    Serial.print(time_s, 3); Serial.print(',');
    Serial.print(s.alt_m, 2); Serial.print(',');
    Serial.print(s.ax_ms2, 3); Serial.print(',');
    Serial.print(s.ay_ms2, 3); Serial.print(',');
    Serial.print(s.az_ms2, 3); Serial.print(',');
    Serial.print(s.gx_rad_s, 3); Serial.print(',');
    Serial.print(s.gy_rad_s, 3); Serial.print(',');
    Serial.print(s.gz_rad_s, 3); Serial.print(',');
    Serial.println(s.temp_C, 2);

    if (i % 100 == 0) {
      delay(5); // avoid USB buffer overflow
    }
  }

  Serial.println("=== END FLASH DUMP ===");
}
