/**
 * ESP32 AirTag Web Tracker v3.0
 * 
 * Receives distance data directly from ESP32 via BLE characteristic notifications.
 * The ESP32 reads RSSI and calculates distance, then sends it to the web app.
 */

// ══════════════════════════════════════════════════════════════
// CONFIGURATION - Must match ESP32 firmware UUIDs
// ══════════════════════════════════════════════════════════════
const CONFIG = {
    DEVICE_NAME: "Neelash's GasTag",
    SERVICE_UUID: "8ec76ea3-6668-48da-9866-75be8bc86f4d",
    DISTANCE_CHAR_UUID: "8ec76ea3-6668-48da-9866-75be8bc86f4e",
    RSSI_CHAR_UUID: "8ec76ea3-6668-48da-9866-75be8bc86f4f",
    CALIBRATE_CHAR_UUID: "8ec76ea3-6668-48da-9866-75be8bc86f50",
    TIMEOUT_MS: 3000,
};

// ══════════════════════════════════════════════════════════════
// UI CONTROLLER
// ══════════════════════════════════════════════════════════════
class TrackerUI {
    constructor() {
        this.elements = {
            connectScreen: document.getElementById('connectScreen'),
            trackerScreen: document.getElementById('trackerScreen'),
            connectBtn: document.getElementById('connectBtn'),
            calibrateBtn: document.getElementById('calibrateBtn'),
            distanceValue: document.getElementById('distanceValue'),
            rssiValue: document.getElementById('rssiValue'),
            confValue: document.getElementById('confValue'),
            statusBar: document.getElementById('statusBar'),
            errorContainer: document.getElementById('errorContainer'),
            auraCore: document.getElementById('auraCore'),
            rings: [
                document.getElementById('ring1'),
                document.getElementById('ring2'),
                document.getElementById('ring3'),
                document.getElementById('ring4')
            ]
        };

        this.currentColor = '#3F3F46';
    }

    showConnect() {
        this.elements.connectScreen.classList.remove('hidden');
        this.elements.trackerScreen.classList.remove('active');
    }

    showTracker() {
        this.elements.connectScreen.classList.add('hidden');
        this.elements.trackerScreen.classList.add('active');
    }

    showError(message) {
        this.elements.errorContainer.innerHTML = `<div class="error-message">${message}</div>`;
    }

    clearError() {
        this.elements.errorContainer.innerHTML = '';
    }

    setStatus(text, color = null) {
        this.elements.statusBar.textContent = text;
        this.elements.statusBar.style.color = color || 'var(--text-secondary)';
    }

    getColorForDistance(distance) {
        if (distance <= 1.0) return '#10B981';
        if (distance <= 3.0) return '#34D399';
        if (distance <= 6.0) return '#FBBF24';
        if (distance <= 10.0) return '#F97316';
        return '#EF4444';
    }

    updateDisplay(data) {
        if (!data) {
            this.elements.distanceValue.textContent = '—';
            this.elements.distanceValue.style.color = 'var(--accent-dim)';
            this.elements.rssiValue.textContent = '-- dBm';
            this.elements.confValue.textContent = 'WAITING...';
            this.setAuraColor('#3F3F46');
            return;
        }

        const { distance, rssi, rawRssi } = data;
        const color = this.getColorForDistance(distance);

        // Distance
        const distText = distance < 10 ? distance.toFixed(1) : distance.toFixed(0);
        this.elements.distanceValue.textContent = distText;
        this.elements.distanceValue.style.color = 'var(--text-primary)';

        // Info
        this.elements.rssiValue.textContent = `${rssi.toFixed(0)} dBm (RAW: ${rawRssi})`;
        
        // Show proximity description
        let proxDesc = 'VERY FAR';
        if (distance <= 1.0) proxDesc = 'VERY CLOSE';
        else if (distance <= 3.0) proxDesc = 'CLOSE';
        else if (distance <= 6.0) proxDesc = 'NEARBY';
        else if (distance <= 10.0) proxDesc = 'FAR';
        
        this.elements.confValue.textContent = proxDesc;
        this.elements.confValue.style.color = color;

        // Aura
        this.setAuraColor(color);
    }

    setAuraColor(color) {
        this.currentColor = color;
        this.elements.auraCore.style.borderColor = color;
        this.elements.rings.forEach(ring => {
            ring.style.borderColor = color;
        });
    }

    showUnsupported() {
        const template = document.getElementById('unsupportedTemplate');
        document.querySelector('.container').innerHTML = '';
        document.querySelector('.container').appendChild(template.content.cloneNode(true));
        this.elements.statusBar.textContent = 'BLUETOOTH NOT AVAILABLE';
    }
}

// ══════════════════════════════════════════════════════════════
// BLUETOOTH TRACKER
// ══════════════════════════════════════════════════════════════
class BluetoothTracker {
    constructor(ui) {
        this.ui = ui;
        this.device = null;
        this.server = null;
        this.distanceChar = null;
        this.calibrateChar = null;
        this.isConnected = false;
        this.lastSeen = 0;
        this.deviceName = null;
    }

    async connect() {
        try {
            this.ui.clearError();
            this.ui.setStatus('SCANNING...');

            // Request the device
            this.device = await navigator.bluetooth.requestDevice({
                filters: [
                    { name: CONFIG.DEVICE_NAME }
                ],
                optionalServices: [CONFIG.SERVICE_UUID]
            });

            if (!this.device) {
                throw new Error('No device selected');
            }

            this.deviceName = this.device.name || 'ESP32-AirTag';
            this.ui.setStatus('CONNECTING...', '#FBBF24');

            // Connect to GATT server
            this.device.addEventListener('gattserverdisconnected', () => this.onDisconnect());
            this.server = await this.device.gatt.connect();

            // Get service
            const service = await this.server.getPrimaryService(CONFIG.SERVICE_UUID);

            // Get distance characteristic and subscribe to notifications
            this.distanceChar = await service.getCharacteristic(CONFIG.DISTANCE_CHAR_UUID);
            await this.distanceChar.startNotifications();
            this.distanceChar.addEventListener('characteristicvaluechanged', (event) => {
                this.handleDistanceData(event.target.value);
            });

            // Get calibration characteristic for writing
            try {
                this.calibrateChar = await service.getCharacteristic(CONFIG.CALIBRATE_CHAR_UUID);
            } catch (e) {
                console.log('Calibration characteristic not available');
            }

            this.isConnected = true;
            this.lastSeen = Date.now();
            this.ui.showTracker();
            this.ui.setStatus(`CONNECTED: ${this.deviceName.toUpperCase()}`, '#10B981');

            // Start timeout checker
            this.startTimeoutChecker();

        } catch (error) {
            console.error('Connection error:', error);
            this.ui.showError(this.getErrorMessage(error));
            this.ui.setStatus('CONNECTION FAILED', '#EF4444');
        }
    }

    handleDistanceData(dataView) {
        try {
            // Data format from ESP32:
            // Bytes 0-3: distance (float, little endian)
            // Byte 4: raw RSSI (as positive value)
            // Bytes 5-8: smoothed RSSI (float, little endian)

            const distance = dataView.getFloat32(0, true);  // Little endian
            const rawRssi = -dataView.getUint8(4);          // Convert back to negative
            const smoothedRssi = dataView.getFloat32(5, true);

            this.lastSeen = Date.now();

            this.ui.updateDisplay({
                distance: distance,
                rssi: smoothedRssi,
                rawRssi: rawRssi
            });

            const color = this.ui.getColorForDistance(distance);
            this.ui.setStatus(`TRACKING: ${this.deviceName.toUpperCase()}`, color);

        } catch (e) {
            console.error('Error parsing distance data:', e);
        }
    }

    startTimeoutChecker() {
        setInterval(() => {
            if (this.isConnected && Date.now() - this.lastSeen > CONFIG.TIMEOUT_MS) {
                this.ui.updateDisplay(null);
                this.ui.setStatus('WAITING FOR DATA...', 'var(--text-secondary)');
            }
        }, 500);
    }

    async calibrate() {
        if (!this.calibrateChar) {
            this.ui.setStatus('CALIBRATION NOT AVAILABLE', '#EF4444');
            return;
        }

        try {
            // Send calibration command (0x01 = auto-calibrate at current position)
            const data = new Uint8Array([0x01]);
            await this.calibrateChar.writeValue(data);
            
            this.ui.setStatus('CALIBRATED AT 1 METER!', '#10B981');
            setTimeout(() => {
                if (this.isConnected) {
                    this.ui.setStatus(`TRACKING: ${this.deviceName.toUpperCase()}`, '#10B981');
                }
            }, 2000);
        } catch (e) {
            console.error('Calibration error:', e);
            this.ui.setStatus('CALIBRATION FAILED', '#EF4444');
        }
    }

    onDisconnect() {
        this.isConnected = false;
        this.ui.updateDisplay(null);
        this.ui.setStatus('DISCONNECTED', '#EF4444');

        setTimeout(() => {
            this.ui.showConnect();
            this.ui.setStatus('READY TO CONNECT');
        }, 2000);
    }

    getErrorMessage(error) {
        if (error.name === 'NotFoundError') {
            return 'No ESP32-AirTag found. Make sure it\'s powered on and nearby.';
        }
        if (error.name === 'SecurityError') {
            return 'Bluetooth permission denied. Please allow access.';
        }
        if (error.name === 'NotSupportedError') {
            return 'Web Bluetooth not supported. Use Chrome on Android or Bluefy on iOS.';
        }
        if (error.message.includes('GATT')) {
            return 'Connection failed. Try moving closer to the device.';
        }
        return error.message || 'Failed to connect to device.';
    }

    disconnect() {
        if (this.device && this.device.gatt.connected) {
            this.device.gatt.disconnect();
        }
    }
}

// ══════════════════════════════════════════════════════════════
// MAIN APP
// ══════════════════════════════════════════════════════════════
class App {
    constructor() {
        this.ui = new TrackerUI();
        this.tracker = null;

        this.init();
    }

    init() {
        // Check Web Bluetooth support
        if (!navigator.bluetooth) {
            this.ui.showUnsupported();
            return;
        }

        // Bind events
        this.ui.elements.connectBtn.addEventListener('click', () => this.startTracking());
        this.ui.elements.calibrateBtn.addEventListener('click', () => this.calibrate());
    }

    async startTracking() {
        this.tracker = new BluetoothTracker(this.ui);
        await this.tracker.connect();
    }

    calibrate() {
        if (this.tracker) {
            this.tracker.calibrate();
        }
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    new App();
});
