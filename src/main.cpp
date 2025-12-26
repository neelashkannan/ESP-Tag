/**
 * ESP32-S3 BLE Beacon with Distance Tracking
 * 
 * This firmware:
 * 1. Broadcasts as a BLE beacon
 * 2. When a phone connects, reads the RSSI of the connection
 * 3. Calculates distance and sends it back via BLE characteristic
 * 4. Phone displays real-time distance without needing to calculate RSSI itself
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <FastLED.h>
#include <esp_mac.h>
#include <esp_gap_ble_api.h>
#include <math.h>

// ============== Board Configuration ==============
#define RGB_LED_PIN     48      // Built-in WS2812 RGB LED
#define NUM_LEDS        1
#define LED_BRIGHTNESS  50

// ============== BLE Configuration ==============
#define DEVICE_NAME "ESP32-AirTag"
#define SERVICE_UUID        "8ec76ea3-6668-48da-9866-75be8bc86f4d"
#define DISTANCE_CHAR_UUID  "8ec76ea3-6668-48da-9866-75be8bc86f4e"  // Distance characteristic
#define RSSI_CHAR_UUID      "8ec76ea3-6668-48da-9866-75be8bc86f4f"  // Raw RSSI characteristic
#define CALIBRATE_CHAR_UUID "8ec76ea3-6668-48da-9866-75be8bc86f50"  // Calibration characteristic

#define MANUFACTURER_ID 0xFFFF
#define ADVERTISING_INTERVAL 100

// ============== Distance Calculation Constants ==============
float measuredPower = -45.0;    // RSSI at 1 meter (calibratable)
float pathLossExponent = 2.5;   // Environment factor (2.0=free space, 4.0=indoor)

// ============== Kalman Filter for RSSI smoothing ==============
class KalmanFilter {
private:
    float q;  // Process noise
    float r;  // Measurement noise
    float x;  // Estimated value
    float p;  // Error covariance
    bool initialized;
    
public:
    KalmanFilter(float processNoise = 0.02, float measurementNoise = 4.0) 
        : q(processNoise), r(measurementNoise), x(0), p(1.0), initialized(false) {}
    
    float update(float measurement) {
        if (!initialized) {
            x = measurement;
            initialized = true;
            return x;
        }
        
        // Prediction
        p = p + q;
        
        // Update
        float k = p / (p + r);
        x = x + k * (measurement - x);
        p = (1 - k) * p;
        
        return x;
    }
    
    float getValue() { return x; }
    void reset() { initialized = false; }
};

// ============== Global Objects ==============
CRGB leds[NUM_LEDS];
BLEServer* pServer = nullptr;
BLECharacteristic* pDistanceChar = nullptr;
BLECharacteristic* pRssiChar = nullptr;
BLECharacteristic* pCalibrateChar = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint16_t connId = 0;
esp_bd_addr_t connectedDeviceAddr;

KalmanFilter rssiFilter(0.02, 4.0);
KalmanFilter distFilter(0.01, 0.3);

uint8_t deviceId[4];
unsigned long lastLedUpdate = 0;
unsigned long lastRssiRead = 0;

// ============== LED Functions ==============
void setLedColor(CRGB color) {
    leds[0] = color;
    FastLED.show();
}

void setLedOff() {
    leds[0] = CRGB::Black;
    FastLED.show();
}

void blinkLed(CRGB color, int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        setLedColor(color);
        delay(delayMs);
        setLedOff();
        delay(delayMs);
    }
}

void showStartupAnimation() {
    for (int i = 0; i < 256; i += 8) {
        leds[0] = CHSV(i, 255, LED_BRIGHTNESS);
        FastLED.show();
        delay(15);
    }
    setLedOff();
    delay(200);
    blinkLed(CRGB::Green, 3, 150);
}

void updateStatusLed() {
    unsigned long now = millis();
    
    if (deviceConnected) {
        // Connected: solid green
        setLedColor(CRGB::Green);
    } else {
        // Advertising: breathing cyan
        if (now - lastLedUpdate > 50) {
            lastLedUpdate = now;
            static uint8_t brightness = 0;
            static int8_t direction = 5;
            
            brightness += direction;
            if (brightness >= LED_BRIGHTNESS || brightness <= 0) {
                direction = -direction;
            }
            leds[0] = CHSV(140, 255, brightness);
            FastLED.show();
        }
    }
}

// ============== Distance Calculation ==============
float calculateDistance(float rssi) {
    if (rssi == 0) return -1.0;
    
    // Log-Distance Path Loss Model
    // distance = 10 ^ ((measuredPower - RSSI) / (10 * n))
    float distance = pow(10.0, (measuredPower - rssi) / (10.0 * pathLossExponent));
    return distance;
}

// ============== RSSI Reading Callback ==============
void rssiCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    if (event == ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT) {
        if (param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            int8_t rawRssi = param->read_rssi_cmpl.rssi;
            
            // Apply Kalman filter to smooth RSSI
            float smoothedRssi = rssiFilter.update((float)rawRssi);
            
            // Calculate distance
            float rawDistance = calculateDistance(smoothedRssi);
            float smoothedDistance = distFilter.update(rawDistance);
            
            // Clamp distance to reasonable values
            if (smoothedDistance < 0.1) smoothedDistance = 0.1;
            if (smoothedDistance > 50.0) smoothedDistance = 50.0;
            
            // Update characteristics
            if (pDistanceChar && pRssiChar) {
                // Send distance as a float (4 bytes) + raw RSSI (1 byte) + smoothed RSSI (4 bytes)
                uint8_t distData[9];
                memcpy(distData, &smoothedDistance, 4);      // Bytes 0-3: distance (float)
                distData[4] = (uint8_t)(-rawRssi);           // Byte 4: raw RSSI (as positive)
                memcpy(distData + 5, &smoothedRssi, 4);      // Bytes 5-8: smoothed RSSI (float)
                
                pDistanceChar->setValue(distData, 9);
                pDistanceChar->notify();
                
                // Also update RSSI characteristic separately
                int8_t rssiVal = (int8_t)smoothedRssi;
                pRssiChar->setValue((uint8_t*)&rssiVal, 1);
                pRssiChar->notify();
            }
            
            // Debug output
            static unsigned long lastDebug = 0;
            if (millis() - lastDebug > 500) {
                lastDebug = millis();
                Serial.printf("RSSI: %d dBm (smooth: %.1f) | Distance: %.2f m\n", 
                              rawRssi, smoothedRssi, smoothedDistance);
            }
        }
    }
}

// ============== BLE Callbacks ==============
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
        deviceConnected = true;
        connId = param->connect.conn_id;
        memcpy(connectedDeviceAddr, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        
        Serial.println("Device connected!");
        Serial.printf("Connection ID: %d\n", connId);
        
        // Reset filters for new connection
        rssiFilter.reset();
        distFilter.reset();
        
        blinkLed(CRGB::Green, 2, 100);
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Device disconnected");
        blinkLed(CRGB::Yellow, 2, 200);
    }
};

class CalibrateCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String value = pCharacteristic->getValue();
        if (value.length() >= 4) {
            // Receive new measured power (float)
            memcpy(&measuredPower, value.c_str(), 4);
            Serial.printf("Calibrated! New 1m reference: %.1f dBm\n", measuredPower);
            blinkLed(CRGB::Cyan, 3, 100);
        } else if (value.length() == 1 && value[0] == 0x01) {
            // Trigger calibration: use current smoothed RSSI as 1m reference
            measuredPower = rssiFilter.getValue();
            Serial.printf("Auto-calibrated at 1m: %.1f dBm\n", measuredPower);
            blinkLed(CRGB::Cyan, 3, 100);
        }
    }
};

// ============== BLE Setup ==============
void setupBLE() {
    Serial.println("Initializing BLE...");
    
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // Register GAP callback for RSSI reading
    esp_ble_gap_register_callback(rssiCallback);
    
    // Get MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    memcpy(deviceId, &mac[2], 4);
    
    Serial.println("========================================");
    Serial.printf("Device Name: %s\n", DEVICE_NAME);
    Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println("========================================");
    
    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    // Create BLE Service
    BLEService* pService = pServer->createService(SERVICE_UUID);
    
    // Distance Characteristic (notify)
    pDistanceChar = pService->createCharacteristic(
        DISTANCE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pDistanceChar->addDescriptor(new BLE2902());
    
    // RSSI Characteristic (notify)
    pRssiChar = pService->createCharacteristic(
        RSSI_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pRssiChar->addDescriptor(new BLE2902());
    
    // Calibration Characteristic (write)
    pCalibrateChar = pService->createCharacteristic(
        CALIBRATE_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCalibrateChar->setCallbacks(new CalibrateCallbacks());
    
    // Start service
    pService->start();
    
    // Configure advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    
    BLEDevice::startAdvertising();
    Serial.println("BLE started advertising!");
}

// ============== Setup ==============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n========================================");
    Serial.println("   ESP32 AirTag with Distance Tracking");
    Serial.println("========================================\n");
    
    // Initialize LED
    FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    Serial.println("RGB LED initialized");
    
    showStartupAnimation();
    setupBLE();
    
    Serial.println("\n✓ Ready!");
    Serial.println("✓ Connect with the web app to track distance");
    Serial.println("✓ Green LED = connected, Cyan breathing = advertising\n");
}

// ============== Main Loop ==============
void loop() {
    // Handle reconnection
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("Restarted advertising...");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
    
    // Read RSSI periodically when connected
    if (deviceConnected) {
        if (millis() - lastRssiRead > 100) {  // Read every 100ms
            lastRssiRead = millis();
            esp_ble_gap_read_rssi(connectedDeviceAddr);
        }
    }
    
    updateStatusLed();
    delay(10);
}
