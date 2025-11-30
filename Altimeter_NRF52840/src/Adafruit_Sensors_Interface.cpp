#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_BMP3XX.h>

// Explicit I2C addresses to avoid using the Nano 33 BLE's internal LSM9DS1
// LSM6DSO32: set SDO to GND for 0x6A (recommended to avoid 0x6B conflict with onboard LSM9DS1)
#define LSM6DSO32_ADDR 0x6A
// BMP390: SDO high -> 0x77, SDO low -> 0x76
#define BMP3XX_ADDR    0x77

Adafruit_LSM6DSO32 lsm6dso32;
Adafruit_BMP3XX bmp;

// sea level pressure for your location (adjust as needed)
#define SEALEVELPRESSURE_HPA (1013.25)

// Helper to check if an I2C device acknowledges at a given address
static bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Initializing sensors (external LSM6DSO32 + BMP390)...");

  // Initialize I2C
  Wire.begin();
  Wire.setClock(400000); // fast-mode I2C

  // Initialize LSM6DSO32 at the explicit address
  if (!lsm6dso32.begin_I2C(LSM6DSO32_ADDR, &Wire)) {
    Serial.println("Failed to find external LSM6DSO32 at expected address!");
    Serial.println("Check wiring and the SDO pin/address.");
    while (1) delay(10);
  }
  Serial.println("LSM6DSO32 connected.");

  // Configure accelerometer and gyro ranges
  lsm6dso32.setAccelRange(LSM6DSO32_ACCEL_RANGE_8_G);
  lsm6dso32.setGyroRange(LSM6DS_GYRO_RANGE_2000_DPS);
  lsm6dso32.setAccelDataRate(LSM6DS_RATE_104_HZ);
  lsm6dso32.setGyroDataRate(LSM6DS_RATE_104_HZ);

  // Initialize BMP390 at the explicit address
  if (!bmp.begin_I2C(BMP3XX_ADDR, &Wire)) {
    Serial.println("Failed to find BMP390 at expected address!");
    Serial.println("Check wiring and the SDO pin/address.");
    while (1) delay(10);
  }
  Serial.println("BMP390 connected.");

  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP3_ODR_50_HZ);
}

void loop() {
  // Check for sensor presence; if any disconnected, report once and skip this loop
  bool lsm_ok = i2cDevicePresent(LSM6DSO32_ADDR);
  bool bmp_ok = i2cDevicePresent(BMP3XX_ADDR);
  if (!lsm_ok || !bmp_ok) {
    Serial.println("Sensor disconnected");
    delay(500);
    return;
  }

  // --- Read LSM6DSO32 ---
  sensors_event_t accel, gyro, temp;
  lsm6dso32.getEvent(&accel, &gyro, &temp);

  Serial.print("Accel (m/s^2): ");
  Serial.print(accel.acceleration.x, 3); Serial.print(", ");
  Serial.print(accel.acceleration.y, 3); Serial.print(", ");
  Serial.println(accel.acceleration.z, 3);

  Serial.print("Gyro (rad/s): ");
  Serial.print(gyro.gyro.x, 3); Serial.print(", ");
  Serial.print(gyro.gyro.y, 3); Serial.print(", ");
  Serial.println(gyro.gyro.z, 3);

  // --- Read BMP390 ---
  if (!bmp.performReading()) {
    Serial.println("Sensor disconnected");
    return;
  }

  Serial.print("Temperature (°C): ");
  Serial.println(bmp.temperature, 2);

  Serial.print("Pressure (hPa): ");
  Serial.println(bmp.pressure / 100.0, 2);

  Serial.print("Altitude (m): ");
  Serial.println(bmp.readAltitude(SEALEVELPRESSURE_HPA), 2);

  Serial.println("------------------------------------");
  delay(200);
}
