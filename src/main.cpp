/**
 * ESP32-S3 BLE Beacon (AirTag-like)
 * Detected: ESP32-S3 with 8MB PSRAM
 * 
 * This firmware turns the ESP32-S3 into a BLE beacon that broadcasts
 * its presence similar to an Apple AirTag. The Python app can detect
 * this beacon and estimate direction based on RSSI.
 * 
 * Hardware Features Used:
 * - Built-in RGB LED on GPIO48 (WS2812/NeoPixel)
 * - USB-C for programming and serial
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <FastLED.h>
#include <esp_mac.h>

// ============== Board Configuration ==============
// ESP32-S3 DevKit specific pins
#define RGB_LED_PIN     48      // Built-in WS2812 RGB LED
#define NUM_LEDS        1       // Single RGB LED on board
#define LED_BRIGHTNESS  50      // 0-255 (lower = less power)

// ============== BLE Configuration ==============
#define DEVICE_NAME "ESP32-AirTag"
#define BEACON_UUID "8ec76ea3-6668-48da-9866-75be8bc86f4d"
#define MANUFACTURER_ID 0xFFFF  // Custom manufacturer ID
#define ADVERTISING_INTERVAL 30  // Ultra-fast advertising (30ms) for real-time tracking

// ============== Global Objects ==============
CRGB leds[NUM_LEDS];
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Unique device identifier (last 4 bytes of MAC address)
uint8_t deviceId[4];

// Battery level simulation (in real application, read from ADC)
uint8_t batteryLevel = 100;

// LED animation state
unsigned long lastLedUpdate = 0;

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
    // Rainbow startup animation
    for (int i = 0; i < 256; i += 8) {
        leds[0] = CHSV(i, 255, LED_BRIGHTNESS);
        FastLED.show();
        delay(15);
    }
    setLedOff();
    delay(200);
    
    // Green blinks = ready
    blinkLed(CRGB::Green, 3, 150);
}

void updateStatusLed() {
    unsigned long now = millis();
    
    if (deviceConnected) {
        // Connected: solid blue
        setLedColor(CRGB::Blue);
    } else {
        // Advertising: breathing cyan effect
        if (now - lastLedUpdate > 50) {
            lastLedUpdate = now;
            
            // Create breathing effect
            static uint8_t brightness = 0;
            static int8_t direction = 5;
            
            brightness += direction;
            if (brightness >= LED_BRIGHTNESS || brightness <= 0) {
                direction = -direction;
            }
            
            leds[0] = CHSV(140, 255, brightness); // Cyan hue
            FastLED.show();
        }
    }
}

void showProximityAlert() {
    // Flash red/white when device connects
    for (int i = 0; i < 5; i++) {
        setLedColor(CRGB::Red);
        delay(100);
        setLedColor(CRGB::White);
        delay(100);
    }
}

// ============== BLE Callbacks ==============
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Device connected!");
        showProximityAlert();
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Device disconnected");
        blinkLed(CRGB::Yellow, 2, 200);
    }
};

// ============== BLE Setup ==============
void setupBLE() {
    Serial.println("Initializing BLE...");
    
    // Initialize BLE
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setPower(ESP_PWR_LVL_P9);  // Max power for better range
    
    // Get MAC address for unique device ID
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    memcpy(deviceId, &mac[2], 4);
    
    Serial.println("----------------------------------------");
    Serial.printf("Device Name: %s\n", DEVICE_NAME);
    Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("Device ID:   %02X%02X%02X%02X\n", 
                  deviceId[0], deviceId[1], deviceId[2], deviceId[3]);
    Serial.printf("Beacon UUID: %s\n", BEACON_UUID);
    Serial.println("----------------------------------------");
    
    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    // Create BLE Service
    BLEService* pService = pServer->createService(BEACON_UUID);
    
    // Create Battery Level Characteristic (standard BLE battery service)
    pCharacteristic = pService->createCharacteristic(
        BLEUUID((uint16_t)0x2A19),  // Battery Level UUID
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setValue(&batteryLevel, 1);
    
    // Start service
    pService->start();
    
    // Configure advertising data
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BEACON_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    
    // Set custom manufacturer data for easy identification
    BLEAdvertisementData advertisementData;
    advertisementData.setName(DEVICE_NAME);
    advertisementData.setCompleteServices(BLEUUID(BEACON_UUID));
    
    // Format: [Manufacturer ID (2)] + [Device ID (4)] + [Battery (1)] + [Version (1)]
    uint8_t mfgData[8];
    mfgData[0] = MANUFACTURER_ID & 0xFF;
    mfgData[1] = (MANUFACTURER_ID >> 8) & 0xFF;
    mfgData[2] = deviceId[0];
    mfgData[3] = deviceId[1];
    mfgData[4] = deviceId[2];
    mfgData[5] = deviceId[3];
    mfgData[6] = batteryLevel;
    mfgData[7] = 0x01;  // Protocol version
    advertisementData.setManufacturerData(String((char*)mfgData, 8));
    
    pAdvertising->setAdvertisementData(advertisementData);
    
    // Start advertising
    BLEDevice::startAdvertising();
    Serial.println("BLE Beacon started advertising!");
}

// ============== Setup ==============
void setup() {
    // Initialize Serial
    Serial.begin(115200);
    delay(1000);  // Wait for serial monitor
    
    Serial.println("\n========================================");
    Serial.println("   ESP32-C6 AirTag Beacon Starting");
    Serial.println("   Board: QS-ESP32-C6 N16 (16MB)");
    Serial.println("========================================\n");
    
    // Initialize FastLED for WS2812 RGB LED
    FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    
    Serial.println("RGB LED initialized on GPIO8");
    
    // Show startup animation
    showStartupAnimation();
    
    // Setup BLE
    setupBLE();
    
    Serial.println("\n✓ Beacon is ready!");
    Serial.println("✓ Use the Python tracker app to find this device");
    Serial.println("✓ LED breathing cyan = advertising");
    Serial.println("✓ LED solid blue = connected\n");
}

// ============== Main Loop ==============
void loop() {
    // Handle connection state changes
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);  // Give the bluetooth stack time to get ready
        pServer->startAdvertising();  // Restart advertising
        Serial.println("Restarted advertising...");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
    
    // Update battery level simulation (decrease every 60 seconds)
    static unsigned long lastBatteryUpdate = 0;
    if (millis() - lastBatteryUpdate > 60000) {
        lastBatteryUpdate = millis();
        if (batteryLevel > 0) {
            batteryLevel--;
            pCharacteristic->setValue(&batteryLevel, 1);
            if (deviceConnected) {
                pCharacteristic->notify();
            }
            Serial.printf("Battery level: %d%%\n", batteryLevel);
            
            // Show low battery warning
            if (batteryLevel <= 20 && batteryLevel % 5 == 0) {
                blinkLed(CRGB::Orange, 2, 300);
            }
        }
    }
    
    // Update status LED
    updateStatusLed();
    
    // Print status periodically
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 10000) {
        lastStatus = millis();
        Serial.printf("[Status] %s | Battery: %d%%\n", 
                      deviceConnected ? "Connected" : "Advertising",
                      batteryLevel);
    }
    
    delay(10);
}
