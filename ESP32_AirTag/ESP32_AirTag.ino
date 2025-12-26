/**
 * ESP32-C6 BLE Beacon (AirTag-like)
 * Board: QS-ESP32-C6 N16 (nanoESP32-C6) - 16MB Flash
 * 
 * ARDUINO IDE SETUP:
 * 1. Add ESP32 board URL: https://espressif.github.io/arduino-esp32/package_esp32_index.json
 * 2. Install "esp32 by Espressif Systems" board package (v3.0+)
 * 3. Select Board: "ESP32C6 Dev Module"
 * 4. Set Flash Size: 16MB
 * 5. Set USB CDC On Boot: Enabled
 * 
 * Hardware:
 * - WS2812 RGB LED on GPIO8
 * - USB-C for programming and serial
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============== Board Configuration ==============
#define RGB_LED_PIN     8       // WS2812 RGB LED on QS-ESP32-C6 N16
#define BUILTIN_LED     8       // Same as RGB LED

// ============== BLE Configuration ==============
#define DEVICE_NAME "ESP32-AirTag"
#define BEACON_UUID "8ec76ea3-6668-48da-9866-75be8bc86f4d"
#define MANUFACTURER_ID 0xFFFF

// ============== Global Objects ==============
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

uint8_t deviceId[4];
uint8_t batteryLevel = 100;

// ============== Simple LED Functions ==============
// Note: For WS2812, we use simple on/off. For full RGB, install FastLED library
void ledOn() {
    neopixelWrite(RGB_LED_PIN, 0, 50, 50);  // Cyan color
}

void ledOff() {
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
}

void ledColor(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(RGB_LED_PIN, r, g, b);
}

void blinkLed(uint8_t r, uint8_t g, uint8_t b, int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        ledColor(r, g, b);
        delay(delayMs);
        ledOff();
        delay(delayMs);
    }
}

void showStartupAnimation() {
    // Simple color cycle
    ledColor(50, 0, 0);   delay(200);  // Red
    ledColor(0, 50, 0);   delay(200);  // Green
    ledColor(0, 0, 50);   delay(200);  // Blue
    ledColor(50, 50, 0);  delay(200);  // Yellow
    ledColor(0, 50, 50);  delay(200);  // Cyan
    ledColor(50, 0, 50);  delay(200);  // Magenta
    ledOff();
    delay(200);
    blinkLed(0, 50, 0, 3, 150);  // Green blinks = ready
}

// ============== BLE Callbacks ==============
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Device connected!");
        // Flash red/white for proximity alert
        for (int i = 0; i < 5; i++) {
            ledColor(50, 0, 0);
            delay(100);
            ledColor(50, 50, 50);
            delay(100);
        }
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Device disconnected");
        blinkLed(50, 50, 0, 2, 200);  // Yellow blinks
    }
};

// ============== BLE Setup ==============
void setupBLE() {
    Serial.println("Initializing BLE...");
    
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    
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
    Serial.println("----------------------------------------");
    
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    BLEService* pService = pServer->createService(BEACON_UUID);
    
    pCharacteristic = pService->createCharacteristic(
        BLEUUID((uint16_t)0x2A19),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setValue(&batteryLevel, 1);
    
    pService->start();
    
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BEACON_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    
    BLEAdvertisementData advertisementData;
    advertisementData.setName(DEVICE_NAME);
    advertisementData.setCompleteServices(BLEUUID(BEACON_UUID));
    
    std::string manufacturerData;
    manufacturerData += (char)(MANUFACTURER_ID & 0xFF);
    manufacturerData += (char)((MANUFACTURER_ID >> 8) & 0xFF);
    manufacturerData += (char)deviceId[0];
    manufacturerData += (char)deviceId[1];
    manufacturerData += (char)deviceId[2];
    manufacturerData += (char)deviceId[3];
    manufacturerData += (char)batteryLevel;
    manufacturerData += (char)0x01;
    advertisementData.setManufacturerData(manufacturerData);
    
    pAdvertising->setAdvertisementData(advertisementData);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE Beacon started advertising!");
}

// ============== Setup ==============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n========================================");
    Serial.println("   ESP32-C6 AirTag Beacon Starting");
    Serial.println("   Board: QS-ESP32-C6 N16 (16MB)");
    Serial.println("========================================\n");
    
    showStartupAnimation();
    setupBLE();
    
    Serial.println("\n✓ Beacon is ready!");
    Serial.println("✓ Use Python tracker app to find this device");
    Serial.println("✓ LED cyan breathing = advertising");
    Serial.println("✓ LED blue = connected\n");
}

// ============== Main Loop ==============
unsigned long lastLedUpdate = 0;
uint8_t brightness = 0;
int8_t direction = 5;

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
    
    // Battery simulation
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
        }
    }
    
    // LED status animation
    if (millis() - lastLedUpdate > 50) {
        lastLedUpdate = millis();
        
        if (deviceConnected) {
            ledColor(0, 0, 50);  // Solid blue when connected
        } else {
            // Breathing cyan effect when advertising
            brightness += direction;
            if (brightness >= 50 || brightness <= 0) {
                direction = -direction;
            }
            ledColor(0, brightness, brightness);
        }
    }
    
    // Status print
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 10000) {
        lastStatus = millis();
        Serial.printf("[Status] %s | Battery: %d%%\n", 
                      deviceConnected ? "Connected" : "Advertising",
                      batteryLevel);
    }
    
    delay(10);
}
