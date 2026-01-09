#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// Unique UUIDs for MakersTag
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DISTANCE_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define RSSI_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CALIB_CHAR_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// Pin for S3-DevKit RGB LED
#define LED_PIN 48 
#define RGB_BRIGHTNESS 60

Adafruit_NeoPixel pixels(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// Kalman Filter for smoothing
class KalmanFilter {
public:
    KalmanFilter(float q, float r, float p, float initial_value) : q(q), r(r), p(p), x(initial_value) {}
    float update(float measurement) {
        p = p + q;
        float k = p / (p + r);
        x = x + k * (measurement - x);
        p = (1 - k) * p;
        return x;
    }
    float getValue() { return x; }
private:
    float q; // process noise covariance
    float r; // measurement noise covariance
    float p; // estimation error covariance
    float x; // value
};

BLEServer* pServer = NULL;
BLECharacteristic* pDistanceChar = NULL;
BLECharacteristic* pRssiChar = NULL;
BLECharacteristic* pCalibChar = NULL;
bool deviceConnected = false;

// Calibration constants (Defaults tuned for ESP32-S3)
float measuredPower = -55.0; 
float pathLossExponent = 2.4;

// Filters: RSSI noise is high (r=4.0), Distance noise is lower (r=0.3)
KalmanFilter rssiFilter(0.02, 4.0, 1.0, -60.0);
KalmanFilter distFilter(0.01, 0.3, 1.0, 1.0);

float calculateDistance(float rssi) {
    if (rssi >= 0) return 0.1;
    float d = pow(10.0, (measuredPower - rssi) / (10.0 * pathLossExponent));
    return (d < 0.1) ? 0.1 : (d > 30.0) ? 30.0 : d; // Clamp
}

void updateVisuals(float dist) {
    if (!deviceConnected) {
        // Pulsing Blue when disconnected
        static float pulse = 0;
        pulse += 0.1;
        uint8_t brightness = (sin(pulse) + 1.0) * 80;
        pixels.setPixelColor(0, pixels.Color(0, 0, brightness));
    } else {
        // Color transition: GREEN (Close) -> YELLOW -> RED (Far)
        int r = 0, g = 0;
        if (dist < 1.0) {
            g = 255; r = 0; // Solid Green
        } else if (dist < 3.5) {
            float ratio = (dist - 1.0) / 2.5;
            r = 255 * ratio;
            g = 255 * (1.0 - ratio);
        } else {
            r = 255; g = 0; // Solid Red
        }
        pixels.setPixelColor(0, pixels.Color(r, g, 0));
    }
    pixels.show();
}

class CalibCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            // If app writes to this characteristic, we recalibrate 1m mark
            // Use current smoothed RSSI as the new measuredPower
            measuredPower = rssiFilter.getValue();
            Serial.printf("Recalibrated! New MeasuredPower (1m): %.2f\n", measuredPower);
            
            // Visual confirmation: Quick Green Flash
            pixels.setPixelColor(0, pixels.Color(0, 255, 0));
            pixels.show();
            delay(200);
        }
    }
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("App connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("App disconnected");
      // Restart advertising
      NimBLEDevice::getAdvertising()->start();
    }
};

void setup() {
  Serial.begin(115200);
  pixels.begin();
  pixels.setBrightness(RGB_BRIGHTNESS);
  pixels.setPixelColor(0, pixels.Color(100, 100, 100)); // White on boot
  pixels.show();

  Serial.println("Starting MakersTag Precision Firmware...");

  NimBLEDevice::init("MakersTag");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); 

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService* pService = pServer->createService(SERVICE_UUID);
  
  pDistanceChar = pService->createCharacteristic(
                      DISTANCE_CHAR_UUID,
                      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
                    );

  pRssiChar = pService->createCharacteristic(
                  RSSI_CHAR_UUID,
                  NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
                );

  pCalibChar = pService->createCharacteristic(
                   CALIB_CHAR_UUID,
                   NIMBLE_PROPERTY::WRITE
                 );
  pCalibChar->setCallbacks(new CalibCallbacks());

  pService->start();

  BLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setMinInterval(0x20); // 20ms
  pAdvertising->setMaxInterval(0x40); // 40ms
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  
  Serial.println("Advertising active...");
}

void loop() {
  if (deviceConnected) {
    // When connected, we use the client's RSSI if we could read it, 
    // but in ESP32 NimBLE server mode, we read the remote RSSI.
    int rssi = 0;
    auto peerDevices = pServer->getPeerDevices();
    for(auto handle : peerDevices) {
        int8_t rssi_val;
        if (ble_gap_conn_rssi(handle, &rssi_val) == 0) {
            rssi = rssi_val;
        }
        break; 
    }

    if (rssi != 0) {
        float smoothRssi = rssiFilter.update((float)rssi);
        float rawDist = calculateDistance(smoothRssi);
        float smoothDist = distFilter.update(rawDist);

        // Update Hardware Feedback
        updateVisuals(smoothDist);

        // Update characteristics
        char distStr[10];
        dtostrf(smoothDist, 1, 2, distStr);
        pDistanceChar->setValue(distStr);
        pDistanceChar->notify();

        char rssiStr[10];
        itoa((int)smoothRssi, rssiStr, 10);
        pRssiChar->setValue(rssiStr);
        pRssiChar->notify();

        Serial.printf("RSSI: %d | Smooth: %.1f | Dist: %.2fm\n", rssi, smoothRssi, smoothDist);
    }
  } else {
    updateVisuals(0); // Handles disconnected pulse
  }
  delay(50); // 20Hz polling for smooth data & visual feedback
}
