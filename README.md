# ESP32-C6 AirTag Clone

A DIY AirTag-like BLE beacon using the **QS-ESP32-C6 N16** board with a **Web App** and Python tracker.

## 🌐 Live Web Tracker

**[Open Web Tracker](https://neelashkannan.github.io/ESP-Tag/)** — Track your ESP32 AirTag directly from your phone's browser!

> Works on Chrome/Edge (Android), Samsung Internet, or Bluefy (iOS)

## 📁 Project Structure

```
esp32-airtag/
├── platformio.ini          # PlatformIO configuration
├── src/
│   └── main.cpp            # ESP32 BLE beacon firmware
├── docs/
│   ├── index.html          # Web tracker (GitHub Pages)
│   ├── tracker.js          # Web Bluetooth tracking logic
│   └── manifest.json       # PWA manifest
├── python-tracker/
│   ├── tracker.py          # Python GUI tracker application
│   └── requirements.txt    # Python dependencies
└── README.md
```

## 🔧 Hardware: QS-ESP32-C6 N16 Board

| Specification | Value |
|---------------|-------|
| Module | ESP32-C6-WROOM-1-N16 |
| Flash | 16MB |
| CPU | RISC-V 160MHz |
| SRAM | 512KB |
| LED | **WS2812 RGB (GPIO8)** |
| USB | TYPE-C |
| Size | 54mm x 28mm |

## 🚀 ESP32 Firmware Setup

### Prerequisites

1. Install [VS Code](https://code.visualstudio.com/)
2. Install [PlatformIO Extension](https://platformio.org/install/ide?install=vscode)

### Build & Upload

1. Open the `esp32-airtag` folder in VS Code
2. PlatformIO will automatically detect the project
3. Connect your QS-ESP32-C6 N16 via USB-C
4. Click **Upload** (→) in the PlatformIO toolbar or run:

```bash
cd esp32-airtag
pio run --target upload
```

### Monitor Serial Output

```bash
pio device monitor
```

You should see:
```
========================================
   ESP32-C6 AirTag Beacon Starting
   Board: QS-ESP32-C6 N16 (16MB)
========================================

RGB LED initialized on GPIO8
Device Name: ESP32-AirTag
MAC Address: XX:XX:XX:XX:XX:XX
Device ID: XXXXXXXX
BLE Beacon started advertising!

✓ Beacon is ready!
✓ Use the Python tracker app to find this device
✓ LED breathing cyan = advertising
✓ LED solid blue = connected
```

### LED Status Indicators

| LED Color | Pattern | Meaning |
|-----------|---------|---------|
| 🌈 Rainbow | Animation | Startup |
| 🟢 Green | 3 blinks | Ready |
| 🔵 Cyan | Breathing | Advertising (searching) |
| 🔵 Blue | Solid | Device connected |
| 🔴 Red/White | Flash | Proximity alert |
| 🟡 Yellow | 2 blinks | Disconnected |
| 🟠 Orange | 2 blinks | Low battery warning |

---

## 📱 Web Tracker (Mobile)

The easiest way to track your ESP32 AirTag is using the **Web Bluetooth Tracker**. No app installation required!

### Supported Browsers

| Platform | Browser | Status |
|----------|---------|--------|
| Android | Chrome | ✅ Full Support |
| Android | Edge | ✅ Full Support |
| Android | Samsung Internet | ✅ Full Support |
| iOS | Bluefy | ✅ Requires Bluefy Browser |
| iOS | Safari | ❌ Not Supported |
| Desktop | Chrome | ⚠️ Limited (better on mobile) |

### How to Use

1. **Open the tracker**: [neelashkannan.github.io/ESP-Tag](https://neelashkannan.github.io/ESP-Tag/)
2. Tap **"Start Scanning"**
3. Select **"ESP32-AirTag"** from the device list
4. Watch the real-time distance estimation!

### Calibration (for accurate distance)

1. Place your phone exactly **1 meter** from the ESP32
2. Tap **"Calibrate at 1m"**
3. The tracker will use this reference point for distance calculations

### Install as App (PWA)

On Android Chrome:
1. Open the tracker website
2. Tap the **menu (⋮)** → **"Add to Home Screen"**
3. Launch from your home screen like a native app!

### iOS Users

iOS Safari doesn't support Web Bluetooth. Use the **Bluefy** browser instead:
1. Install [Bluefy from App Store](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
2. Open the tracker URL in Bluefy
3. Grant Bluetooth permissions when prompted

---

## 🐍 Python Tracker Setup

### Prerequisites

- Python 3.8+
- Bluetooth enabled on your computer

### Install Dependencies

```bash
cd python-tracker
pip install -r requirements.txt
```

### Run the Tracker

```bash
python tracker.py
```

### macOS Permissions

On macOS, you may need to grant Bluetooth permissions:
1. Go to **System Preferences** → **Security & Privacy** → **Privacy**
2. Select **Bluetooth** from the left sidebar
3. Add your Terminal app or Python to the allowed list

### Linux Permissions

On Linux, you may need to run with elevated privileges or configure BlueZ:
```bash
sudo python tracker.py
# OR configure capabilities
sudo setcap 'cap_net_raw,cap_net_admin+eip' $(which python3)
```

## 📱 How It Works

### ESP32 Beacon

The ESP32-C6 broadcasts BLE advertisements containing:
- **Device Name**: `ESP32-AirTag`
- **Service UUID**: Custom UUID for identification
- **Manufacturer Data**: Device ID + Battery level
- **Advertising Interval**: 100ms for responsive tracking

### Python Tracker

The tracker app:
1. Scans for BLE devices matching our beacon signature
2. Measures **RSSI** (Received Signal Strength Indicator)
3. Estimates distance using the path-loss formula
4. Displays a visual direction indicator

### Distance Estimation

Distance is estimated using:
```
distance = 10 ^ ((TxPower - RSSI) / (10 * n))
```
Where:
- `TxPower` = Expected RSSI at 1 meter (-59 dBm default)
- `RSSI` = Measured signal strength
- `n` = Path loss exponent (2.5 for indoor environments)

## 🎯 Signal Strength Guide

| RSSI Range | Signal | Approximate Distance |
|------------|--------|---------------------|
| > -50 dBm  | Excellent | < 1 meter |
| -50 to -60 | Very Good | 1-2 meters |
| -60 to -70 | Good | 2-5 meters |
| -70 to -80 | Fair | 5-10 meters |
| -80 to -90 | Weak | 10-20 meters |
| < -90 dBm  | Very Weak | > 20 meters |

## ⚙️ Customization

### Adjust TX Power (Range vs Battery)

In `main.cpp`, modify:
```cpp
BLEDevice::setPower(ESP_PWR_LVL_P9);  // Max power
```
Options: `ESP_PWR_LVL_N12` to `ESP_PWR_LVL_P9`

### Calibrate Distance Estimation

In `tracker.py`, adjust based on your environment:
```python
TX_POWER_AT_1M = -59  # Measure actual RSSI at 1 meter
```

### Change Device Name

In `main.cpp`:
```cpp
#define DEVICE_NAME "MyCustomTag"
```

## 🔋 Power Optimization

For battery-powered operation:

1. Increase advertising interval:
   ```cpp
   #define ADVERTISING_INTERVAL 500  // 500ms
   ```

2. Use lower TX power:
   ```cpp
   BLEDevice::setPower(ESP_PWR_LVL_N3);
   ```

3. Enable deep sleep between advertisements (advanced)

## 🐛 Troubleshooting

### Beacon Not Found

1. Check ESP32 is powered and LED is blinking
2. Verify Bluetooth is enabled on computer
3. Check serial monitor for errors
4. Try moving closer to the beacon

### Inaccurate Distance

1. Calibrate `TX_POWER_AT_1M` for your environment
2. Avoid metal objects and walls between devices
3. RSSI naturally fluctuates; use averages

### macOS "Bluetooth permission" Error

Grant Terminal/Python Bluetooth access in System Preferences.

### Linux "Operation not permitted"

Run with `sudo` or configure Bluetooth capabilities.

## 📄 License

MIT License - Feel free to use and modify!

## 🔮 Future Improvements

- [x] Web-based mobile tracker (GitHub Pages)
- [ ] Add sound/haptic feedback when close
- [ ] Multiple beacon tracking
- [ ] Direction finding with multiple receivers
- [ ] Native mobile app (iOS/Android)
- [ ] UWB support for precise positioning (ESP32-U4 required)
- [ ] Battery monitoring with actual ADC reading
- [ ] OTA firmware updates
