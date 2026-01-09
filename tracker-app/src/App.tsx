import React, { useState } from 'react';

// UUIDs must match the ESP32 firmware
const SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
const DISTANCE_CHAR_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';
const RSSI_CHAR_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a9';
const CALIB_CHAR_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26aa';

const App: React.FC = () => {
  const [distance, setDistance] = useState<number | null>(null);
  const [rssi, setRssi] = useState<number | null>(null);
  const [isScanning, setIsScanning] = useState(false);
  const [status, setStatus] = useState('Ready to find');
  const [device, setDevice] = useState<BluetoothDevice | null>(null);
  const [calibChar, setCalibChar] = useState<BluetoothRemoteGATTCharacteristic | null>(null);

  const startScan = async () => {
    try {
      setIsScanning(true);
      if (!navigator.bluetooth) {
        setStatus('Web Bluetooth not supported');
        return;
      }
      
      setStatus('Searching...');
      
      const selectedDevice = await navigator.bluetooth.requestDevice({
        filters: [{ services: [SERVICE_UUID] }],
        optionalServices: [SERVICE_UUID]
      });

      setDevice(selectedDevice);
      setStatus('Connecting...');
      
      const server = await selectedDevice.gatt?.connect();
      const service = await server?.getPrimaryService(SERVICE_UUID);

      // Distance Characteristic
      const distChar = await service?.getCharacteristic(DISTANCE_CHAR_UUID);
      await distChar?.startNotifications();
      distChar?.addEventListener('characteristicvaluechanged', (event: any) => {
        const value = new TextDecoder().decode(event.target.value);
        setDistance(parseFloat(value));
      });

      // RSSI Characteristic
      const rssiChar = await service?.getCharacteristic(RSSI_CHAR_UUID);
      await rssiChar?.startNotifications();
      rssiChar?.addEventListener('characteristicvaluechanged', (event: any) => {
        const value = new TextDecoder().decode(event.target.value);
        setRssi(parseInt(value));
      });

      // Calibration Characteristic
      const cChar = await service?.getCharacteristic(CALIB_CHAR_UUID);
      setCalibChar(cChar);

      setStatus('High-Precision Tracking');

      selectedDevice.addEventListener('gattserverdisconnected', () => {
        setDevice(null);
        setDistance(null);
        setRssi(null);
        setCalibChar(null);
        setStatus('Disconnected');
        setIsScanning(false);
      });

    } catch (error) {
      console.error(error);
      setStatus('Connection Failed');
      setIsScanning(false);
    }
  };

  const handleCalibrate = async () => {
    if (!calibChar) return;
    try {
      // Send any byte to trigger recalibration on ESP32
      await calibChar.writeValue(new Uint8Array([1]));
      alert('1-Meter Calibration Complete. ESP32 has saved the new baseline.');
    } catch (e) {
      console.error('Calibration failed', e);
    }
  };

  return (
    <div className="flex flex-col items-center justify-center min-h-screen bg-black text-white p-6 relative overflow-hidden font-sans">
      {/* Premium Radar Background */}
      <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
        <div className="radar-ring w-[300px] h-[300px]"></div>
        <div className="radar-ring w-[600px] h-[600px]" style={{ animationDelay: '1s' }}></div>
        <div className="radar-ring w-[900px] h-[900px]" style={{ animationDelay: '2s' }}></div>
      </div>

      <div className="z-10 text-center w-full max-w-sm">
        <header className="mb-12">
           <h1 className="text-3xl font-light tracking-[0.4em] text-white">MAKERS TAG</h1>
           <p className="text-[10px] tracking-[0.3em] text-blue-500 mt-2 uppercase font-semibold">Precision Edition</p>
        </header>

        <div className="relative mb-20 flex justify-center">
          <div className="w-72 h-72 rounded-full border border-white/10 flex items-center justify-center bg-white/[0.02] shadow-[0_0_80px_rgba(59,130,246,0.15)] relative backdrop-blur-sm">
            {distance !== null ? (
              <div className="animate-in fade-in zoom-in duration-1000">
                <div className="flex items-baseline justify-center">
                   <span className="text-8xl font-thin tracking-tighter text-white">
                     {distance.toFixed(1)}
                   </span>
                   <span className="text-xl ml-1 font-light text-white/40">m</span>
                </div>
                <div className="mt-4 flex flex-col items-center justify-center space-y-1">
                   <div className="flex items-center space-x-2">
                      <div className="w-2 h-2 rounded-full bg-blue-500 animate-pulse"></div>
                      <span className="text-[10px] tracking-[0.2em] text-blue-400 font-medium uppercase">Signal: {rssi} dBm</span>
                   </div>
                </div>
              </div>
            ) : (
                <div className="text-center px-8">
                   <div className="w-16 h-16 border-2 border-white/10 border-t-white/80 rounded-full animate-spin mx-auto mb-6"></div>
                   <p className="text-[10px] tracking-[0.4em] text-white/30 uppercase">{status}</p>
                </div>
            )}
          </div>
        </div>

        <button
          onClick={startScan}
          disabled={!!device || isScanning}
          className="w-full py-5 rounded-full bg-white text-black text-xs font-bold tracking-[0.3em] uppercase transition-all active:scale-[0.95] disabled:bg-neutral-900 disabled:text-white/20 hover:bg-neutral-200"
        >
          {device ? 'SYSTEM CONNECTED' : isScanning ? 'SEARCHING...' : 'START FINDING'}
        </button>

        {device && (
          <button
            onClick={handleCalibrate}
            className="w-full mt-4 py-3 rounded-full border border-white/20 text-white/40 text-[9px] font-bold tracking-[0.2em] uppercase transition-all hover:bg-white/5 active:scale-[0.95]"
          >
            Calibrate 1m Distance
          </button>
        )}
      </div>

      <footer className="absolute bottom-10 inset-x-0 text-center px-8">
         <div className="max-w-xs mx-auto">
            <p className="text-[8px] tracking-[0.5em] text-white/20 uppercase font-light">
              Dual-Stage Kalman Filter · ESP32-S3 Precision
            </p>
         </div>
      </footer>
    </div>
  );
};

export default App;
