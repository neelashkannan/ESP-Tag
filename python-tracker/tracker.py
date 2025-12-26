#!/usr/bin/env python3
"""
ESP32-AirTag Advanced Distance Tracker (v2.0)
Latest Research-Based Approach:
- Dual-Stage Adaptive Kalman Filtering (RSSI & Distance domains)
- Log-Distance Path Loss with Dynamic PLE (Path Loss Exponent)
- Real-time Environmental Calibration
"""

import asyncio
import sys
import math
import time
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional, List

try:
    from bleak import BleakScanner
except ImportError:
    print("Error: bleak library not found. Install with: pip install bleak")
    sys.exit(1)

try:
    import tkinter as tk
    from tkinter import font as tkfont
    from tkinter import ttk
except ImportError:
    print("Error: tkinter not found. Install with: brew install python-tk@3.14")
    sys.exit(1)

# ══════════════════════════════════════════════════════════════
# ADVANCED CONFIGURATION
# ══════════════════════════════════════════════════════════════
DEVICE_NAME = "ESP32-AirTag"
BEACON_UUID = "8ec76ea3-6668-48da-9866-75be8bc86f4d"

# Default Constants
DEFAULT_MEASURED_POWER = -45  # RSSI at 1 meter
N_FREE_SPACE = 2.0
N_INDOOR = 3.5
# ══════════════════════════════════════════════════════════════

class AdaptiveKalmanFilter:
    def __init__(self, q_base=0.01, r=5.0):
        self.q_base = q_base
        self.r = r
        self.x = None
        self.p = 1.0
        self.prev_innovation = 0

    def update(self, z: float) -> float:
        if self.x is None:
            self.x = z
            return z
        innovation = z - self.x
        innovation_change = abs(innovation - self.prev_innovation)
        adaptive_q = self.q_base * (1 + min(innovation_change / 5.0, 5.0))
        self.prev_innovation = innovation
        self.p = self.p + adaptive_q
        k = self.p / (self.p + self.r)
        self.x = self.x + k * innovation
        self.p = (1 - k) * self.p
        return self.x

class DistanceKalmanFilter:
    def __init__(self, process_noise=0.02, measurement_noise=0.4):
        self.q = process_noise
        self.r = measurement_noise
        self.x = None
        self.p = 1.0

    def update(self, z: float) -> float:
        if self.x is None:
            self.x = z
            return z
        self.p = self.p + self.q
        k = self.p / (self.p + self.r)
        self.x = self.x + k * (z - self.x)
        self.p = (1 - k) * self.p
        return self.x

class AdvancedDistanceEstimator:
    def __init__(self):
        self.rssi_filter = AdaptiveKalmanFilter(q_base=0.02, r=4.0)
        self.dist_filter = DistanceKalmanFilter()
        self.rssi_window = deque(maxlen=20)
        self.measured_power = DEFAULT_MEASURED_POWER
        self.n_exponent = N_FREE_SPACE
        
    def calibrate(self, current_rssi: float):
        """Sets the reference RSSI at 1 meter."""
        self.measured_power = current_rssi
        print(f"Calibrated: 1m Reference = {self.measured_power:.1f} dBm")

    def estimate(self, raw_rssi: float) -> tuple[float, float, float]:
        # 1. Smooth RSSI
        smoothed_rssi = self.rssi_filter.update(raw_rssi)
        self.rssi_window.append(raw_rssi)
        
        # 2. Adaptive PLE (n) based on signal variance
        if len(self.rssi_window) > 10:
            mean = sum(self.rssi_window) / len(self.rssi_window)
            variance = sum((x - mean)**2 for x in self.rssi_window) / len(self.rssi_window)
            std_dev = math.sqrt(variance)
            # Higher variance = more multipath = higher n
            self.n_exponent = N_FREE_SPACE + (min(std_dev, 8.0) / 8.0) * (N_INDOOR - N_FREE_SPACE)
            confidence = max(0, 100 - (std_dev * 10))
        else:
            confidence = 50
            
        # 3. Log-Distance Path Loss
        distance_raw = 10 ** ((self.measured_power - smoothed_rssi) / (10 * self.n_exponent))
        
        # 4. Distance Domain Smoothing
        smoothed_distance = self.dist_filter.update(distance_raw)
        
        return smoothed_distance, smoothed_rssi, confidence

@dataclass
class Beacon:
    address: str
    name: str
    last_seen: datetime = field(default_factory=datetime.now)
    last_raw: int = -100
    estimator: AdvancedDistanceEstimator = field(default_factory=AdvancedDistanceEstimator)
    
    _distance: float = 0.0
    _smoothed_rssi: float = -100.0
    _confidence: float = 0.0
    
    def add_measurement(self, rssi: int):
        self.last_raw = rssi
        self._distance, self._smoothed_rssi, self._confidence = self.estimator.estimate(float(rssi))
        self.last_seen = datetime.now()

    @property
    def distance(self) -> float: return self._distance
    @property
    def rssi(self) -> float: return self._smoothed_rssi
    @property
    def confidence(self) -> float: return self._confidence

class SignalTrackerUI:
    BG_COLOR = "#09090B"
    TEXT_PRIMARY = "#FAFAFA"
    TEXT_SECONDARY = "#71717A"
    ACCENT_DIM = "#27272A"
    
    COLORS = [
        (1.0, "#10B981"), (3.0, "#34D399"), (6.0, "#FBBF24"), (10.0, "#F97316"), (20.0, "#EF4444"),
    ]

    def __init__(self, root):
        self.root = root
        self.root.title("Advanced AirTag Tracker")
        self.root.geometry("400x500")
        self.root.configure(bg=self.BG_COLOR)
        self.root.resizable(False, False)
        
        # Fonts
        try:
            self.font_dist = tkfont.Font(family="SF Pro Display", size=80, weight="bold")
            self.font_unit = tkfont.Font(family="SF Pro Text", size=18, weight="bold")
            self.font_label = tkfont.Font(family="SF Pro Text", size=11)
        except:
            self.font_dist = tkfont.Font(family="Helvetica", size=80, weight="bold")
            self.font_unit = tkfont.Font(family="Helvetica", size=18, weight="bold")
            self.font_label = tkfont.Font(family="Helvetica", size=11)

        # Canvas for Aura
        self.canvas = tk.Canvas(root, width=400, height=380, bg=self.BG_COLOR, highlightthickness=0)
        self.canvas.pack(pady=(20, 0))
        
        # Distance Display
        self.lbl_dist = tk.Label(root, text="—", font=self.font_dist, fg=self.ACCENT_DIM, bg=self.BG_COLOR)
        self.lbl_dist.place(relx=0.5, rely=0.38, anchor="center")
        
        self.lbl_unit = tk.Label(root, text="METERS", font=self.font_unit, fg=self.TEXT_SECONDARY, bg=self.BG_COLOR)
        self.lbl_unit.place(relx=0.5, rely=0.52, anchor="center")
        
        # Bottom Status Bar
        self.status_frame = tk.Frame(root, bg="#18181B", height=60)
        self.status_frame.pack(side="bottom", fill="x")
        self.lbl_status = tk.Label(self.status_frame, text="SCANNING...", font=self.font_label, fg=self.TEXT_SECONDARY, bg="#18181B")
        self.lbl_status.pack(expand=True, pady=20)
        
        self.current_beacon = None
        self.phase = 0.0
        self.current_color = self.ACCENT_DIM
        self.target_color = self.ACCENT_DIM
        self._animate()

    def _lerp_color(self, c1: str, c2: str, t: float) -> str:
        r1, g1, b1 = int(c1[1:3], 16), int(c1[3:5], 16), int(c1[5:7], 16)
        r2, g2, b2 = int(c2[1:3], 16), int(c2[3:5], 16), int(c2[5:7], 16)
        r = int(r1 + (r2 - r1) * t)
        g = int(g1 + (g2 - g1) * t)
        b = int(b1 + (b2 - b1) * t)
        return f"#{r:02x}{g:02x}{b:02x}"

    def _get_color_for_dist(self, dist: float) -> str:
        for threshold, color in self.COLORS:
            if dist <= threshold: return color
        return self.COLORS[-1][1]

    def _animate(self):
        self.canvas.delete("aura")
        self.current_color = self._lerp_color(self.current_color, self.target_color, 0.08)
        self.phase += 0.05
        breath = (math.sin(self.phase) + 1) / 2
        cx, cy = 200, 170
        base_r = 110
        pulse_r = base_r + (breath * 20)
        for i in range(4):
            r = pulse_r + (i * 18)
            opacity = max(0, 0.4 - (i * 0.1))
            ring_color = self._lerp_color(self.BG_COLOR, self.current_color, opacity)
            self.canvas.create_oval(cx - r, cy - r, cx + r, cy + r, outline=ring_color, width=2, tags="aura")
        self.canvas.create_oval(cx - pulse_r, cy - pulse_r, cx + pulse_r, cy + pulse_r, outline=self.current_color, width=5, tags="aura")
        self.root.after(16, self._animate)

    def update_data(self, beacon: Optional[Beacon]):
        self.current_beacon = beacon
        if beacon is None:
            self.lbl_dist.config(text="—", fg=self.ACCENT_DIM)
            self.target_color = self.ACCENT_DIM
            self.lbl_status.config(text="SEARCHING...", fg=self.TEXT_SECONDARY)
            return
        
        dist = beacon.distance
        self.target_color = self._get_color_for_dist(dist)
        
        self.lbl_dist.config(text=f"{dist:.1f}" if dist < 10 else f"{dist:.0f}", fg=self.TEXT_PRIMARY)
        self.lbl_status.config(text=f"CONNECTED: {beacon.name.upper()}", fg=self.target_color)

async def scanner_task(ui: SignalTrackerUI):
    beacons = {}
    def detection_callback(device, advertisement_data):
        if device.name == DEVICE_NAME or (advertisement_data.service_uuids and BEACON_UUID.lower() in [str(u).lower() for u in advertisement_data.service_uuids]):
            addr = device.address
            if addr not in beacons: beacons[addr] = Beacon(address=addr, name=device.name or "AirTag")
            beacons[addr].add_measurement(advertisement_data.rssi)

    scanner = BleakScanner(detection_callback=detection_callback)
    await scanner.start()
    try:
        while True:
            now = datetime.now()
            active = [b for b in beacons.values() if (now - b.last_seen).total_seconds() < 2.5]
            ui.update_data(max(active, key=lambda b: b.rssi) if active else None)
            await asyncio.sleep(0.05)
    finally: await scanner.stop()

async def main():
    root = tk.Tk()
    ui = SignalTrackerUI(root)
    task = asyncio.create_task(scanner_task(ui))
    try:
        while True:
            root.update()
            await asyncio.sleep(0.01)
    except tk.TclError: pass
    finally: task.cancel()

if __name__ == "__main__":
    try: asyncio.run(main())
    except KeyboardInterrupt: pass
