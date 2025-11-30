// Arduino -> CSV stream for Python visualizer
// Outputs: ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,mx_uT,my_uT,mz_uT
// Uses Arduino_BMI270_BMM150 (IMU) API

#include <Arduino.h>
#include <Arduino_BMI270_BMM150.h> // install from Library Manager if needed

const unsigned long SAMPLE_INTERVAL_MS = 33; // ~30 Hz
unsigned long lastMillis = 0;

// last-known values (keeps output steady if a reading is momentarily unavailable)
float ax_g = 0.0f, ay_g = 0.0f, az_g = 0.0f;
float gx_dps = 0.0f, gy_dps = 0.0f, gz_dps = 0.0f;
float mx_uT = 0.0f, my_uT = 0.0f, mz_uT = 0.0f;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(1); }

  Serial.println("IMU CSV stream starting"); // safe debug line (no commas)
  if (!IMU.begin()) {
    Serial.println("IMU.begin() failed. Check wiring/library.");
    // fail fast: blink built-in LED to indicate error
    pinMode(LED_BUILTIN, OUTPUT);
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
      Serial.println("IMU init error");
    }
  }

  // Optional: print reported sample rate if available (some IMU libs provide it)
  Serial.print("IMU initialized. Gyro sample rate: ");
  Serial.print(IMU.gyroscopeSampleRate());
  Serial.println(" Hz");

  lastMillis = millis();
}

void loop() {
  unsigned long now = millis();
  if (now - lastMillis < SAMPLE_INTERVAL_MS) {
    // keep loop responsive
    delay(1);
    return;
  }
  lastMillis = now;

  // Read accelerometer if available (returns m/s^2)
  if (IMU.accelerationAvailable()) {
    float ax_ms2, ay_ms2, az_ms2;
    if (IMU.readAcceleration(ax_ms2, ay_ms2, az_ms2)) {
      const float g_conv = 9.80665f;
      ax_g = ax_ms2 / g_conv;
      ay_g = ay_ms2 / g_conv;
      az_g = az_ms2 / g_conv;
    }
  }

  // Read gyroscope if available (returns deg/s)
  if (IMU.gyroscopeAvailable()) {
    float gx, gy, gz;
    if (IMU.readGyroscope(gx, gy, gz)) {
      gx_dps = gx;
      gy_dps = gy;
      gz_dps = gz;
    }
  }

  // Read magnetometer if available (returns microtesla for BMM150)
  if (IMU.magneticFieldAvailable()) {
    float mx, my, mz;
    if (IMU.readMagneticField(mx, my, mz)) {
      mx_uT = mx;
      my_uT = my;
      mz_uT = mz;
    }
  }

  // Print one CSV line: 9 values
  // Use fixed decimal formatting to keep the Python parser happy
  Serial.print(ax_g, 3); Serial.print(',');
  Serial.print(ay_g, 3); Serial.print(',');
  Serial.print(az_g, 3); Serial.print(',');
  Serial.print(gx_dps, 3); Serial.print(',');
  Serial.print(gy_dps, 3); Serial.print(',');
  Serial.print(gz_dps, 3); Serial.print(',');
  Serial.print(mx_uT, 3); Serial.print(',');
  Serial.print(my_uT, 3); Serial.print(',');
  Serial.println(mz_uT, 3);

  // optional short yield to avoid saturating USB serial
  delay(0);
}
