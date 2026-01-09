# MakersTag: Premium ESP32-S3 Tracker

This project transforms an **ESP32-S3 N16R8** into a trackable BLE tag similar to an AirTag or JioTag, complete with a high-end, minimal web-mobile application for distance tracking.

**[🌐 Live App Demo](https://neelashkannan.github.io/ESP-Tag/)**

## 🚀 Features
- **High-Precision Logic**: Dual-stage Kalman filter (on-board ESP32) for stable distance readings.
- **Hardware Feedback**: RGB LED proximity indicators (Green < 1m, Yellow 2m, Red > 3.5m).
- **One-Tap Calibration**: Calibrate the 1-meter baseline directly from the app.
- **Modern Radar UI**: Minimalist tracker app built with React 19 and Tailwind 4.

---

## 🛠 Hardware Setup (Firmware)

The firmware is located in the `firmware/` directory and is designed for PlatformIO.

### 1. Requirements
- ESP32-S3 (N16R8 variant)
- [PlatformIO VS Code Extension](https://platformio.org/platformio-ide)

### 2. Flashing
1. Open the `firmware/` folder in VS Code.
2. Connect your ESP32-S3 via USB.
3. Click the **PlatformIO: Build** icon, then **PlatformIO: Upload**.
4. The device will start advertising as `MakersTag`.

---

## 📱 App Setup (Tracker)

The app is a Vite + React + Tailwind project located in `tracker-app/`.

### 1. Installation
```bash
cd tracker-app
npm install
```

### 2. Running Locally
```bash
npm run dev
```

### 3. Usage on Mobile
For the best experience (and to access Web Bluetooth), we recommend using **Chrome** on Android or **Bluefy** browser on iOS.
1. Host the app via HTTPS (required for Web Bluetooth). You can use [Vercel](https://vercel.com) or [Netlify](https://netlify.com) for a quick deployment.
2. Open the URL on your phone.
3. Click **START SCAN** and select your `MakersTag`.

---

## 📐 How it Works
The system uses **RSSI (Received Signal Strength Indicator)** to estimate distance. 
- **Near**: RSSI (~ -40 to -60 dBm) translates to < 2m.
- **Far**: RSSI (~ -80 to -100 dBm) translates to > 10m.

*Note: RSSI is affected by walls and obstacles. For high precision, calibration of the `measuredPower` variable in `App.tsx` and `main.cpp` is recommended.*

---

## 💎 Premium UI Design
The UI features a deep black theme with:
- **Radar pulse animations** for a high-tech feel.
- **Minimalist typography** following Apple-like aesthetics.
- **Glassmorphism** indicators for distance and signal strength.
