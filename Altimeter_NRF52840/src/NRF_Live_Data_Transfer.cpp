#include <ArduinoBLE.h>
#include <Arduino_BMI270_BMM150.h>

// Bluetooth Service and Characteristic
BLEService imuService("12345678-1234-1234-1234-123456789ABC");
BLECharacteristic gyroCharacteristic("12345678-1234-1234-1234-123456789ABD", BLERead | BLENotify, 8);

struct GyroPacket {
  int16_t gx, gy, gz;
  uint16_t timestamp;
};

GyroPacket gyroPacket;
unsigned long lastSampleTime = 0;
const long sampleInterval = 50; // 20Hz
bool connected = false;
uint16_t packetCounter = 0;

void setup() {
  // Initialize LED pin
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize IMU - NO SERIAL WAIT!
  if (!IMU.begin()) {
    // If IMU fails, just blink LED and continue
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
    }
  }

  // Initialize BLE - NO SERIAL WAIT!
  if (!BLE.begin()) {
    // If BLE fails, blink fast and continue
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(50);
      digitalWrite(LED_BUILTIN, LOW);
      delay(50);
    }
  }

  // BLE setup
  BLE.setDeviceName("NanoGyro");
  BLE.setLocalName("NanoGyro");
  BLE.setAdvertisedService(imuService);
  imuService.addCharacteristic(gyroCharacteristic);
  BLE.addService(imuService);
  
  // Start advertising
  BLE.advertise();
  
  // Blink LED to indicate ready
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
}

void loop() {
  BLEDevice central = BLE.central();
  
  if (central) {
    if (!connected) {
      connected = true;
      // Solid LED when connected
      digitalWrite(LED_BUILTIN, HIGH);
    }
    
    while (central.connected()) {
      unsigned long currentTime = millis();
      
      if (currentTime - lastSampleTime >= sampleInterval) {
        lastSampleTime = currentTime;
        
        if (IMU.gyroscopeAvailable()) {
          float gx, gy, gz;
          IMU.readGyroscope(gx, gy, gz);
          
          // Pack data
          gyroPacket.gx = (int16_t)(gx * 100);
          gyroPacket.gy = (int16_t)(gy * 100);
          gyroPacket.gz = (int16_t)(gz * 100);
          gyroPacket.timestamp = packetCounter++;
          
          // Send data
          gyroCharacteristic.writeValue(&gyroPacket, sizeof(gyroPacket));
        }
      }
      delay(5); // Small delay for BLE stability
    }
    
    if (connected) {
      connected = false;
      digitalWrite(LED_BUILTIN, LOW);
      // Restart advertising
      BLE.advertise();
    }
  }
}