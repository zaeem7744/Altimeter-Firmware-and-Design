/*
  Project: Altimeter_NRF52840 — Rocket Telemetry Firmware (All-in-One)
  File: utilities/firmware_samples/rocket_telemetry_all_in_one.cpp
  Purpose: Self-contained single-file firmware combining config, state, button, LED, and BLE subsystems.
  Inputs:
    - Button on BUTTON_PIN with INPUT_PULLUP (active-low)
    - BLE central (ArduinoBLE) for connect/notify
    - Time base from millis()
  Outputs:
    - Serial logs (115200)
    - RGB LED PWM for status effects
    - BLE advertising/connection and notifications (UUIDs match desktop client)
  Notes:
    - Target: Arduino Nano 33 BLE (nRF52840)
    - Dependencies: ArduinoBLE, Wire (installed via board libs)
    - This file is independent of project headers; build as a standalone sketch/sample.
*/
#include <Arduino.h>
#include <ArduinoBLE.h>

// ==================== CONFIG (pins, timing) ====================
#define BUTTON_PIN D5
#ifndef LED_RED
#define LED_RED   D9
#endif
#ifndef LED_GREEN
#define LED_GREEN D10
#endif
#ifndef LED_BLUE
#define LED_BLUE  D11
#endif

#define MAIN_LOOP_DELAY 10
#define DEBOUNCE_DELAY 50
#define MULTI_PRESS_WINDOW 500
#define LONG_PRESS_THRESHOLD 10000
#define BT_DISCONNECT_DURATION 5000

// ==================== STATE TYPES ====================
enum DeviceMode {
  MODE_IDLE,
  MODE_LOGGING,
  MODE_BT_ADVERTISING,
  MODE_BT_CONNECTED,
  MODE_BT_DISCONNECTING,
  MODE_MEMORY_CLEAR
};

struct SystemState {
  DeviceMode mode;
  bool logging;
  bool bleActive;
  bool bleConnected;
  unsigned long modeStartTime;
  unsigned long disconnectStartTime;
};

static SystemState systemState = {
  /*mode*/ MODE_IDLE, /*logging*/ false, /*bleActive*/ false, /*bleConnected*/ false,
  /*modeStartTime*/ 0, /*disconnectStartTime*/ 0
};

// ==================== LED INDICATOR ====================
static void setLEDColor(uint8_t red, uint8_t green, uint8_t blue) {
  // Inverted PWM for common-anode wiring (adjust if needed)
  analogWrite(LED_RED, 255 - red);
  analogWrite(LED_GREEN, 255 - green);
  analogWrite(LED_BLUE, 255 - blue);
}

static float smoothWave(unsigned long time, float periodMs) {
  float phase = fmod(static_cast<float>(time), periodMs) / periodMs;
  return 0.5f * (1.0f - cos(2.0f * PI * phase));
}

static void updateLED(unsigned long currentTime) {
  switch(systemState.mode) {
    case MODE_IDLE: {
      float val = smoothWave(currentTime, 2000.0f);
      setLEDColor(0, 0, (uint8_t)(val * 150));
    } break;
    case MODE_LOGGING: {
      float val = smoothWave(currentTime, 4000.0f);
      setLEDColor(0, (uint8_t)(val * 200), 0);
    } break;
    case MODE_BT_ADVERTISING: {
      unsigned long cycle = currentTime % 1000;
      setLEDColor(0, 0, (cycle < 500) ? 150 : 0);
    } break;
    case MODE_BT_CONNECTED: {
      float val = smoothWave(currentTime, 4000.0f);
      setLEDColor(0, (uint8_t)(val * 100), (uint8_t)(val * 200));
    } break;
    case MODE_BT_DISCONNECTING: {
      if (currentTime - systemState.disconnectStartTime >= BT_DISCONNECT_DURATION) {
        systemState.mode = MODE_IDLE;
        systemState.bleActive = false;
        systemState.bleConnected = false;
        break;
      }
      unsigned long cycle = currentTime % 2000;
      setLEDColor((cycle < 600) ? 200 : 0, 0, 0);
    } break;
    case MODE_MEMORY_CLEAR:
      // handled by handleMemoryClearLED()
      break;
  }
}

static void handleMemoryClearLED(unsigned long holdDuration) {
  if (holdDuration < 1000) {
    setLEDColor(0, 0, 0);
    return;
  }
  if (holdDuration <= LONG_PRESS_THRESHOLD) {
    float progress = (float)(holdDuration - 1000) / (float)(LONG_PRESS_THRESHOLD - 1000);
    progress = constrain(progress, 0.0f, 1.0f);
    float period = 1200.0f - (progress * 800.0f);
    float intensity = 0.1f + (progress * 0.9f);
    unsigned long cycleTime = millis() % (unsigned long)period;
    float pulsePhase = (float)cycleTime / period;
    float pulseValue = (pulsePhase < 0.3f) ? 1.0f : 0.0f;
    uint8_t brightness = (uint8_t)(intensity * 255 * pulseValue);
    setLEDColor(brightness, brightness, brightness);
  }
}

// ==================== BLE SERVICE ====================
static BLEService telemetryService("19B10000-E8F2-537E-4F6C-D104768A1214");
static BLEStringCharacteristic dataCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify, 512);
static BLEStringCharacteristic commandCharacteristic("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 512);

static void initializeBLE() {
  if (!BLE.begin()) {
    Serial.println("❌ Failed to initialize BLE!");
    return;
  }
  BLE.setDeviceName("RocketTelemetry");
  BLE.setLocalName("RocketTelemetry");
  BLE.setAdvertisedService(telemetryService);
  telemetryService.addCharacteristic(dataCharacteristic);
  telemetryService.addCharacteristic(commandCharacteristic);
  BLE.addService(telemetryService);
  BLE.advertise();
  systemState.bleActive = true;
  systemState.bleConnected = false;
  systemState.mode = MODE_BT_ADVERTISING;
  systemState.modeStartTime = millis();
  Serial.println("📡 BLE Advertising Started");
}

static void stopBLE() {
  BLE.disconnect();
  BLE.stopAdvertise();
  BLE.end();
  systemState.bleActive = false;
  systemState.bleConnected = false;
  Serial.println("📡 BLE Stopped");
}

static void updateBLE(unsigned long currentTime) {
  if (!systemState.bleActive) return;
  BLEDevice central = BLE.central();
  if (central) {
    if (!systemState.bleConnected) {
      systemState.bleConnected = true;
      systemState.mode = MODE_BT_CONNECTED;
      systemState.modeStartTime = currentTime;
      Serial.println("🔗 BLE Connected");
    }
  } else {
    if (systemState.bleConnected) {
      systemState.bleConnected = false;
      systemState.disconnectStartTime = currentTime;
      systemState.mode = MODE_BT_DISCONNECTING;
      Serial.println("🔌 BLE Disconnected");
    }
  }
}

// ==================== BUTTON INPUT ====================
struct ButtonState {
  bool pressed;
  bool lastState;
  unsigned long pressStartTime;
  unsigned long lastReleaseTime;
  int pressCount;
};

static ButtonState buttonState = { false, HIGH, 0, 0, 0 };

static void handleButton(unsigned long currentTime) {
  int currentButtonState = digitalRead(BUTTON_PIN);

  // Edge: press
  if (currentButtonState == LOW && buttonState.lastState == HIGH) {
    buttonState.pressed = true;
    buttonState.pressStartTime = currentTime;
    buttonState.lastState = LOW;
  }

  // Edge: release
  if (currentButtonState == HIGH && buttonState.lastState == LOW) {
    buttonState.pressed = false;
    unsigned long pressDuration = currentTime - buttonState.pressStartTime;
    buttonState.lastState = HIGH;

    // Long press => memory clear (visual + state reset)
    if (pressDuration >= LONG_PRESS_THRESHOLD) {
      Serial.println("🗑️ Memory Cleared!");
      for (int i = 0; i < 3; i++) { setLEDColor(255,255,255); delay(200); setLEDColor(0,0,0); delay(200);} 
      systemState.mode = MODE_IDLE;
      systemState.logging = false;
      stopBLE();
      return;
    }

    // Count multi-presses
    if (currentTime - buttonState.lastReleaseTime < MULTI_PRESS_WINDOW) {
      buttonState.pressCount++;
    } else {
      buttonState.pressCount = 1;
    }
    buttonState.lastReleaseTime = currentTime;
  }

  // Show memory clear progress while holding
  if (buttonState.pressed) {
    unsigned long holdDuration = currentTime - buttonState.pressStartTime;
    if (holdDuration > 1000) {
      systemState.mode = MODE_MEMORY_CLEAR;
      handleMemoryClearLED(holdDuration);
      return;
    }
  }

  // After timeout, execute press pattern action
  if (!buttonState.pressed && buttonState.pressCount > 0 && 
      (currentTime - buttonState.lastReleaseTime) > MULTI_PRESS_WINDOW) {
    int count = buttonState.pressCount;
    buttonState.pressCount = 0;

    switch (count) {
      case 1: // toggle logging
        systemState.logging = !systemState.logging;
        if (systemState.logging) {
          systemState.mode = MODE_LOGGING;
          systemState.modeStartTime = currentTime;
          Serial.println("🟢 Logging Started");
        } else {
          systemState.mode = MODE_IDLE;
          systemState.modeStartTime = currentTime;
          Serial.println("🔵 Logging Stopped");
        }
        break;
      case 2: // toggle BLE
        if (!systemState.bleActive) {
          initializeBLE();
        } else {
          if (!systemState.bleConnected) {
            stopBLE();
            systemState.mode = MODE_IDLE;
            systemState.modeStartTime = currentTime;
          } else {
            Serial.println("ℹ️ BLE Connected - Use triple press to disconnect");
          }
        }
        break;
      case 3: // force BLE disconnect
        if (systemState.bleActive || systemState.bleConnected) {
          stopBLE();
          systemState.disconnectStartTime = currentTime;
          systemState.mode = MODE_BT_DISCONNECTING;
          Serial.println("🔌 BLE Force Disconnected");
        }
        break;
    }
  }
}

// ==================== ENTRY POINT ====================
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("🚀 Rocket Telemetry System - All-in-One Build");
  Serial.println("================================================");
  Serial.println("✅ System Initialized");
  Serial.println("💡 LED Status: Blue Fade (Idle)");
  Serial.println();
  Serial.println("📋 Button Guide:");
  Serial.println("   Single Press: Toggle Logging");
  Serial.println("   Double Press: Toggle Bluetooth");
  Serial.println("   Triple Press: Force Disconnect BLE");
  Serial.println("   Long Press (10s): Clear Memory");
  Serial.println("================================================");

  systemState.mode = MODE_IDLE;
  systemState.modeStartTime = millis();
}

void loop() {
  unsigned long now = millis();
  handleButton(now);
  updateBLE(now);
  updateLED(now);
  delay(MAIN_LOOP_DELAY);
}
