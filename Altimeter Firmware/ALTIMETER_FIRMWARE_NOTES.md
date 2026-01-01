# Altimeter Firmware – Debugging & Changes Summary

These notes summarize the main problems we faced and the changes made while bringing up the new PlatformIO-based logger and Python tools. Intended for the project owner to review.

## 1. Environment & Projects

- Old PlatformIO project (now deleted): `Altimeter_Firmware` (underscore)
- New working PlatformIO project: `Altimeter Firmware` (space)
  - Main firmware: `src/main.cpp`
  - Flash logger: `src/FlashStorage.{h,cpp}`
  - Python tools: `python_tools/read_flight_log.py`
- Separate Arduino test sketch used for validation: `Arduino_Test_Firmware` folder (serial-only logger that was known-good).

## 2. Serial / COM9 Problems

**Symptoms**
- COM9 would appear for a moment then disappear.
- Serial Monitor sometimes showed random characters (paths, script names) on startup.
- Previous firmware using more aggressive FlashIAP layouts would "kill" the port until reflashed in bootloader mode.

**Root causes**
- Multiple tools competing for the same COM port:
  - PlatformIO Serial Monitor
  - Python scripts
  - Arduino IDE Serial Monitor
- Old FlashIAP code using a `storageBaseAddr` computed from `flashStart + flashSize - dataRegionSize` could overlap with code/bootloader regions, causing crashes while the CPU was executing from flash.
- Serial parsing originally expected full strings (e.g. `DUMP`) and was sensitive to line endings; now everything is single-character commands.

**Fixes**
- Simplified to **single-character commands** read non-blockingly from Serial.
- Added an **ignore window** (`ignoreSerialUntil`) to drop any junk bytes during the first ~1 s after reset.
- Ensured only **one** Serial Monitor / script uses COM9 at a time.

## 3. Logging Design (Current State)

### 3.1 Sensors

- IMU: `Adafruit_LSM6DSO32` (accel)
- Baro: `Adafruit_BMP3XX` (altitude via `readAltitude(SEALEVELPRESSURE_HPA)`).
- Sampling rate: **50 Hz** (`SAMPLE_RATE_HZ = 50`, `SAMPLE_INTERVAL_MS = 20`).

### 3.2 Flash Storage Layout

Implemented in `src/FlashStorage.{h,cpp}`, based on the known-good `data_logger_flash_180_per_sector.cpp` method:

- Uses **FlashIAP** and mbed underneath PlatformIO.
- Flash layout:
  - **Lower 512 KB**: program + bootloader
  - **Upper 512 KB**: data logging region
- Configuration:
  - `SECTORS_COUNT = 96`
  - `SAMPLES_PER_SECTOR = 180`
  - Total capacity: `96 * 180` samples
- Each sample (`SensorSample`) stores:
  - `time_s` (float, seconds since logging start)
  - `altitude_m` (float)
  - `ax_ms2`, `ay_ms2`, `az_ms2` (float accel components)

### 3.3 Flash API

- `flashStorage.begin()`
  - Initializes FlashIAP, sets `storageBaseAddr = flashStart + 512 KB`, checks region size, finds latest sector based on `FLASH_MAGIC` and `sectorSequence`.
- `flashStorage.addSample(time_s, altitude, ax, ay, az)`
  - Buffers samples in RAM (10 at a time) then writes into the current sector.
  - When a sector is full, saves and advances to the next.
- `flashStorage.clearStorage()`
  - Erases all logging sectors and resets state.
- `flashStorage.printStatus()`
  - Prints total samples and capacity.
- `flashStorage.dumpToSerialSeconds()`
  - Walks all sectors in sequence order and streams samples as CSV:
    `time_s,alt_m,ax_ms2,ay_ms2,az_ms2`

## 4. Main Firmware Serial Commands (PlatformIO)

Implemented in `src/main.cpp` (current version):

- `A` – **START** logging to flash (50 samples/s)
  - Resets timing and turns on logging.
- `B` – **STOP** logging
  - Stops adding new samples.
- `S` – **STATUS**
  - Shows logging ON/OFF and flash sample count/capacity.
- `C` – **CLEAR** flash storage
  - Erases all sectors in the logging region.
- `D` – **DUMP** all stored samples from flash as CSV
  - Format: `time_s,alt_m,ax_ms2,ay_ms2,az_ms2`
- `H` – **HELP**
  - Prints a brief command summary.

No live CSV streaming is performed during logging anymore; data is only printed when explicitly requested via `D`.

## 5. Python Tool Integration

File: `python_tools/read_flight_log.py`

- Opens the configured serial port (default `COM9`, `115200` baud).
- Sends the **single-character `D` command** to trigger a dump.
- Reads from Serial until it sees the CSV header (`time_s,alt_m,ax_ms2,ay_ms2,az_ms2`) and the end marker (`=== END FLASH DUMP`).
- Parses data into a pandas DataFrame, cleans non-numeric rows, and saves:
  - `flight_logs/flight_log.csv`
  - `flight_logs/flight_log.xlsx`
- Plots:
  - Relative altitude over time
  - Velocity estimate from altitude derivative
  - Acceleration magnitude

## 6. Key Lessons / Pitfalls

- **Flash layout must respect bootloader/code regions**; using the upper 512 KB with a fixed base address is safer than trying to auto-place at `flashStart + flashSize - dataRegionSize` without knowing the Arduino/PlatformIO core layout.
- **Single-character commands** and a small ignore window make serial control more robust across different monitors and OS behaviors.
- Avoid mixing multiple tools on the same COM port; close PlatformIO monitor before running Python scripts, and vice versa.
