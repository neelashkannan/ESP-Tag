/**
 * ESP32 AirTag Web Tracker
 * Uses Web Bluetooth API for BLE scanning and distance estimation
 */

// ══════════════════════════════════════════════════════════════
// CONFIGURATION - Must match your ESP32 firmware
// ══════════════════════════════════════════════════════════════
const CONFIG = {
    DEVICE_NAME: "ESP32-AirTag",
    SERVICE_UUID: "8ec76ea3-6668-48da-9866-75be8bc86f4d",
    DEFAULT_MEASURED_POWER: -45, // RSSI at 1 meter (calibrate for accuracy)
    N_FREE_SPACE: 2.0,
    N_INDOOR: 3.5,
    SCAN_INTERVAL: 100, // ms between RSSI reads
    TIMEOUT_MS: 3000,   // Consider device lost after this
};

// ══════════════════════════════════════════════════════════════
// ADAPTIVE KALMAN FILTER
// ══════════════════════════════════════════════════════════════
class AdaptiveKalmanFilter {
    constructor(qBase = 0.02, r = 4.0) {
        this.qBase = qBase;
        this.r = r;
        this.x = null;
        this.p = 1.0;
        this.prevInnovation = 0;
    }

    update(z) {
        if (this.x === null) {
            this.x = z;
            return z;
        }
        
        const innovation = z - this.x;
        const innovationChange = Math.abs(innovation - this.prevInnovation);
        const adaptiveQ = this.qBase * (1 + Math.min(innovationChange / 5.0, 5.0));
        this.prevInnovation = innovation;
        
        this.p = this.p + adaptiveQ;
        const k = this.p / (this.p + this.r);
        this.x = this.x + k * innovation;
        this.p = (1 - k) * this.p;
        
        return this.x;
    }

    reset() {
        this.x = null;
        this.p = 1.0;
        this.prevInnovation = 0;
    }
}

// ══════════════════════════════════════════════════════════════
// DISTANCE KALMAN FILTER
// ══════════════════════════════════════════════════════════════
class DistanceKalmanFilter {
    constructor(processNoise = 0.02, measurementNoise = 0.4) {
        this.q = processNoise;
        this.r = measurementNoise;
        this.x = null;
        this.p = 1.0;
    }

    update(z) {
        if (this.x === null) {
            this.x = z;
            return z;
        }
        
        this.p = this.p + this.q;
        const k = this.p / (this.p + this.r);
        this.x = this.x + k * (z - this.x);
        this.p = (1 - k) * this.p;
        
        return this.x;
    }

    reset() {
        this.x = null;
        this.p = 1.0;
    }
}

// ══════════════════════════════════════════════════════════════
// DISTANCE ESTIMATOR
// ══════════════════════════════════════════════════════════════
class DistanceEstimator {
    constructor() {
        this.rssiFilter = new AdaptiveKalmanFilter(0.02, 4.0);
        this.distFilter = new DistanceKalmanFilter();
        this.rssiWindow = [];
        this.windowSize = 20;
        this.measuredPower = CONFIG.DEFAULT_MEASURED_POWER;
        this.nExponent = CONFIG.N_FREE_SPACE;
    }

    calibrate(currentRssi) {
        this.measuredPower = currentRssi;
        console.log(`Calibrated: 1m Reference = ${this.measuredPower.toFixed(1)} dBm`);
    }

    estimate(rawRssi) {
        // 1. Smooth RSSI
        const smoothedRssi = this.rssiFilter.update(rawRssi);
        
        // 2. Update window
        this.rssiWindow.push(rawRssi);
        if (this.rssiWindow.length > this.windowSize) {
            this.rssiWindow.shift();
        }

        // 3. Adaptive PLE based on variance
        let confidence = 50;
        if (this.rssiWindow.length > 10) {
            const mean = this.rssiWindow.reduce((a, b) => a + b, 0) / this.rssiWindow.length;
            const variance = this.rssiWindow.reduce((sum, x) => sum + Math.pow(x - mean, 2), 0) / this.rssiWindow.length;
            const stdDev = Math.sqrt(variance);
            
            this.nExponent = CONFIG.N_FREE_SPACE + 
                (Math.min(stdDev, 8.0) / 8.0) * (CONFIG.N_INDOOR - CONFIG.N_FREE_SPACE);
            confidence = Math.max(0, 100 - (stdDev * 10));
        }

        // 4. Log-Distance Path Loss Model
        const distanceRaw = Math.pow(10, (this.measuredPower - smoothedRssi) / (10 * this.nExponent));

        // 5. Distance domain smoothing
        const smoothedDistance = this.distFilter.update(distanceRaw);

        return {
            distance: smoothedDistance,
            rssi: smoothedRssi,
            confidence: confidence
        };
    }

    reset() {
        this.rssiFilter.reset();
        this.distFilter.reset();
        this.rssiWindow = [];
    }
}

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
            this.elements.confValue.textContent = '--%';
            this.setAuraColor('#3F3F46');
            return;
        }

        const { distance, rssi, confidence, rawRssi } = data;
        const color = this.getColorForDistance(distance);

        // Distance
        const distText = distance < 10 ? distance.toFixed(1) : distance.toFixed(0);
        this.elements.distanceValue.textContent = distText;
        this.elements.distanceValue.style.color = 'var(--text-primary)';

        // Info
        this.elements.rssiValue.textContent = `${rssi.toFixed(0)} dBm (RAW: ${rawRssi})`;
        this.elements.confValue.textContent = `${confidence.toFixed(0)}%`;
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
        this.estimator = new DistanceEstimator();
        this.isConnected = false;
        this.lastSeen = 0;
        this.rssiPollInterval = null;
    }

    async connect() {
        try {
            this.ui.clearError();
            this.ui.setStatus('SCANNING...');

            // Request device with filters
            const options = {
                filters: [
                    { name: CONFIG.DEVICE_NAME },
                    { services: [CONFIG.SERVICE_UUID] }
                ],
                optionalServices: [CONFIG.SERVICE_UUID]
            };

            // Fallback for browsers that don't support name filter well
            try {
                this.device = await navigator.bluetooth.requestDevice(options);
            } catch (e) {
                // Try with acceptAllDevices if filters fail
                this.device = await navigator.bluetooth.requestDevice({
                    acceptAllDevices: true,
                    optionalServices: [CONFIG.SERVICE_UUID]
                });
            }

            if (!this.device) {
                throw new Error('No device selected');
            }

            this.ui.setStatus('CONNECTING...');
            
            // Connect to GATT server
            this.server = await this.device.gatt.connect();
            this.isConnected = true;
            this.lastSeen = Date.now();

            // Setup disconnect handler
            this.device.addEventListener('gattserverdisconnected', () => this.onDisconnect());

            // Show tracker UI
            this.ui.showTracker();
            this.ui.setStatus(`CONNECTED: ${this.device.name || 'ESP32-AirTag'}`, '#10B981');

            // Start RSSI polling
            this.startRssiPolling();

        } catch (error) {
            console.error('Connection error:', error);
            this.ui.showError(this.getErrorMessage(error));
            this.ui.setStatus('CONNECTION FAILED', '#EF4444');
        }
    }

    getErrorMessage(error) {
        if (error.name === 'NotFoundError') {
            return 'No device found. Make sure your ESP32 is powered on and advertising.';
        }
        if (error.name === 'SecurityError') {
            return 'Bluetooth permission denied. Please allow Bluetooth access.';
        }
        if (error.name === 'NotSupportedError') {
            return 'Bluetooth not supported on this device/browser.';
        }
        return error.message || 'Failed to connect to device.';
    }

    startRssiPolling() {
        // Note: Web Bluetooth doesn't directly expose RSSI during connection
        // We need to use a workaround - reading RSSI via device.watchAdvertisements() 
        // or simulating based on connection quality
        
        if ('watchAdvertisements' in this.device) {
            // Modern approach using watchAdvertisements (Chrome 87+)
            this.device.watchAdvertisements().then(() => {
                this.device.addEventListener('advertisementreceived', (event) => {
                    this.handleRssi(event.rssi);
                });
            }).catch(e => {
                console.log('watchAdvertisements not available, using fallback');
                this.startFallbackPolling();
            });
        } else {
            this.startFallbackPolling();
        }
    }

    startFallbackPolling() {
        // Fallback: Reconnect periodically to get fresh RSSI
        // This is a workaround since Web Bluetooth has limited RSSI support
        this.rssiPollInterval = setInterval(async () => {
            if (this.isConnected && this.server && this.server.connected) {
                try {
                    // Try to read a characteristic or just check connection
                    const service = await this.server.getPrimaryService(CONFIG.SERVICE_UUID);
                    this.lastSeen = Date.now();
                    
                    // Simulate RSSI based on connection stability
                    // In real scenarios, you might read a characteristic that contains RSSI
                    // For now, we'll use a placeholder
                    this.ui.setStatus(`CONNECTED: ${this.device.name || 'ESP32-AirTag'}`, '#10B981');
                } catch (e) {
                    // Connection might be degraded
                    console.log('Connection check failed:', e);
                }
            }
        }, CONFIG.SCAN_INTERVAL);
    }

    handleRssi(rssi) {
        if (rssi === null || rssi === undefined) return;
        
        this.lastSeen = Date.now();
        const result = this.estimator.estimate(rssi);
        
        this.ui.updateDisplay({
            distance: result.distance,
            rssi: result.rssi,
            confidence: result.confidence,
            rawRssi: rssi
        });
    }

    calibrate() {
        if (this.estimator.rssiFilter.x !== null) {
            this.estimator.calibrate(this.estimator.rssiFilter.x);
            this.ui.setStatus('CALIBRATED SUCCESSFULLY', '#10B981');
            setTimeout(() => {
                if (this.isConnected) {
                    this.ui.setStatus(`CONNECTED: ${this.device?.name || 'ESP32-AirTag'}`, '#10B981');
                }
            }, 2000);
        }
    }

    onDisconnect() {
        this.isConnected = false;
        this.estimator.reset();
        
        if (this.rssiPollInterval) {
            clearInterval(this.rssiPollInterval);
        }

        this.ui.updateDisplay(null);
        this.ui.setStatus('DISCONNECTED', '#EF4444');
        
        // Return to connect screen after delay
        setTimeout(() => {
            this.ui.showConnect();
            this.ui.setStatus('READY TO CONNECT');
        }, 2000);
    }

    async disconnect() {
        if (this.device && this.device.gatt.connected) {
            this.device.gatt.disconnect();
        }
    }
}

// ══════════════════════════════════════════════════════════════
// SCANNING TRACKER (Alternative approach using BLE scanning)
// ══════════════════════════════════════════════════════════════
class ScanningTracker {
    constructor(ui) {
        this.ui = ui;
        this.estimator = new DistanceEstimator();
        this.isScanning = false;
        this.lastSeen = 0;
        this.abortController = null;
    }

    async startScanning() {
        if (!('bluetooth' in navigator) || !('requestLEScan' in navigator.bluetooth)) {
            // Fall back to connection-based approach
            return false;
        }

        try {
            this.ui.clearError();
            this.ui.setStatus('SCANNING...');

            this.abortController = new AbortController();

            const scan = await navigator.bluetooth.requestLEScan({
                filters: [
                    { name: CONFIG.DEVICE_NAME },
                    { services: [CONFIG.SERVICE_UUID] }
                ],
                keepRepeatedDevices: true
            }, { signal: this.abortController.signal });

            this.isScanning = true;
            this.ui.showTracker();

            navigator.bluetooth.addEventListener('advertisementreceived', (event) => {
                this.handleAdvertisement(event);
            });

            // Check for timeout
            this.startTimeoutCheck();

            return true;
        } catch (error) {
            console.log('Scanning not available:', error);
            return false;
        }
    }

    handleAdvertisement(event) {
        const rssi = event.rssi;
        this.lastSeen = Date.now();

        const result = this.estimator.estimate(rssi);
        
        this.ui.updateDisplay({
            distance: result.distance,
            rssi: result.rssi,
            confidence: result.confidence,
            rawRssi: rssi
        });

        this.ui.setStatus(`TRACKING: ${event.device.name || 'ESP32-AirTag'}`, 
            this.ui.getColorForDistance(result.distance));
    }

    startTimeoutCheck() {
        setInterval(() => {
            if (this.isScanning && Date.now() - this.lastSeen > CONFIG.TIMEOUT_MS) {
                this.ui.updateDisplay(null);
                this.ui.setStatus('SEARCHING FOR BEACON...', 'var(--text-secondary)');
            }
        }, 500);
    }

    calibrate() {
        if (this.estimator.rssiFilter.x !== null) {
            this.estimator.calibrate(this.estimator.rssiFilter.x);
            this.ui.setStatus('CALIBRATED SUCCESSFULLY', '#10B981');
        }
    }

    stopScanning() {
        if (this.abortController) {
            this.abortController.abort();
        }
        this.isScanning = false;
    }
}

// ══════════════════════════════════════════════════════════════
// MAIN APP
// ══════════════════════════════════════════════════════════════
class App {
    constructor() {
        this.ui = new TrackerUI();
        this.tracker = null;
        this.scanningTracker = null;

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

        // Handle visibility changes
        document.addEventListener('visibilitychange', () => {
            if (document.hidden && this.tracker) {
                // Could pause scanning to save battery
            }
        });
    }

    async startTracking() {
        // Try scanning approach first (better for continuous tracking)
        this.scanningTracker = new ScanningTracker(this.ui);
        const scanningAvailable = await this.scanningTracker.startScanning();

        if (!scanningAvailable) {
            // Fall back to connection-based approach
            this.tracker = new BluetoothTracker(this.ui);
            await this.tracker.connect();
        }
    }

    calibrate() {
        if (this.scanningTracker && this.scanningTracker.isScanning) {
            this.scanningTracker.calibrate();
        } else if (this.tracker && this.tracker.isConnected) {
            this.tracker.calibrate();
        }
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    new App();
});
