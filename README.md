# APEX: Adaptive Statistical Appliance Health Monitor

A low-cost, edge-intelligent predictive maintenance monitor for rotating appliances (such as a 5V DC fan) powered by the ESP32. APEX performs statistical baseline calibration, 4-band FFT vibration analysis, and hardware safety latching, connected to a local Node.js backend and real-time React dashboard.

## Key Features

- **Dual-Core Edge Intelligence**: Core 1 handles 100 Hz sensor sampling, FFT energy computation, baseline calibration, and safety latch decisions. Core 0 handles JSON telemetry serialization and bidirectional USB serial communication.
- **Statistical Baseline Calibration**: Automatically learns healthy operating baseline values (mean and standard deviation) for dynamic vibration RMS, 4 FFT frequency bands, dominant frequency, and temperature.
- **Staged Anomaly Detection**:
  - **Warning**: Detects mild imbalance, rising friction, or abnormal temperature rise without cutting power.
  - **Critical (Latched)**: Automatically cuts appliance power via relay when severe vibration or high temperature is detected. Requires an explicit user reset command to clear.
- **Offline First**: Runs completely offline without cloud dependencies. The ESP32 streams telemetry directly over USB Serial to a local Node.js/Express server and SQLite database, displayed on a Vite React dashboard.

## System Architecture

```
                 [ 100 Hz Micros Timing ]
       MPU6050 (Clone 0x74) + DS18B20 + ADS1115 / ACS712
                         |
            +------------+------------+
            |    ESP32 CORE 1 TASK    | (Priority 2)
            | * 128-sample window     |
            | * 4-Band FFT Energy     |
            | * Baseline Calibration  |
            | * Statistical Rules     |
            | * Safety Latch Logic    |
            | * Relay & LED Control   |
            +------------+------------+
                         | Shared Telemetry Snapshot
            +------------+------------+
            |    ESP32 CORE 0 TASK    | (Priority 1)
            | * USB Serial Streamer   |
            | * JSON Command Receiver |
            +------------+------------+
                         | USB Serial (115200 Baud)
            +------------+------------+
            |   LOCAL BACKEND SERVER  | (Node.js & Express)
            | * SerialPort Ingestion  |
            | * SQLite Data Storage   |
            | * REST API Endpoints    |
            | * Static Web Server     |
            +------------+------------+
                         | HTTP (Port 5000)
            +------------+------------+
            |  VITE REACT DASHBOARD   | (Local Browser)
            | * Real-time Gauges      |
            | * State & Latch Status  |
            | * Historical Charts     |
            | * Manual / Auto Control |
            | * Calibration & Resets  |
            +-------------------------+
```

## Hardware Wiring Reference

| Peripheral | ESP32 GPIO | Description and Configuration |
| :--- | :--- | :--- |
| **MPU6050 (Clone)** | SDA = `GPIO 21`<br>SCL = `GPIO 22` | Clone `WHO_AM_I = 0x74`. Direct I2C registers: Wake (`0x6B = 0x00`), DLPF ~21 Hz (`0x1A = 0x04`), Range +/-8g (`0x1C = 0x10`), 400 kHz clock. |
| **DS18B20 Probe** | `GPIO 4` | OneWire temperature probe with 4.7k pull-up to 3.3V. Non-blocking conversion across 128-sample window. |
| **ADS1115 (A0)** | SDA = `GPIO 21`<br>SCL = `GPIO 22` | Address `0x48`, `GAIN_ONE` (+/-4.096V). Reads ACS712 current via 22k/10k voltage divider (ratio 10/32). Optional via `#define USE_CURRENT`. |
| **Relay Module** | `GPIO 26` | `HIGH` = Fan ON (Energized), `LOW` = Fan OFF (Power Cut). Controls fan power. |
| **Green LED** | `GPIO 32` | Indicates NORMAL and CALIBRATING states (330 ohm resistor). |
| **Yellow LED** | `GPIO 33` | Indicates WARNING state (330 ohm resistor). |
| **Red LED** | `GPIO 25` | Indicates CRITICAL (LATCHED) and SENSOR ERROR states (330 ohm resistor). |
| **Active Buzzer** | `GPIO 27` | Active HIGH. Sounds during CRITICAL state unless muted by command. |

## Health States and Actuator Behavior

| State | Relay Output | Green LED | Yellow LED | Red LED | Buzzer | Latch Behavior |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **NORMAL** | ON (Energized) | ON | OFF | OFF | OFF | Unlatched |
| **CALIBRATING** | ON (Energized) | ON | OFF | OFF | OFF | Unlatched (Progress bar shown on dashboard) |
| **WARNING** | ON (Energized) | OFF | ON | OFF | OFF | Unlatched (Warning shown on dashboard) |
| **CRITICAL** | **OFF (Power Cut)** | OFF | OFF | **ON** | **ON** | **LATCHED** (Requires `{"reset": true}` command) |
| **SENSOR_ERROR** | ON (Energized) | OFF | OFF | **ON** | OFF | Unlatched (Recovers automatically when reconnected) |

## Detection Logic and Decision Rules

### 1. Baseline Calibration
- Triggered automatically on first boot or on receiving `{"calibrate": true}`.
- Runs for 140 windows of 128 samples (approximately 3 minutes) while the fan operates normally.
- Computes mean and standard deviation for:
  - Dynamic vibration RMS (acceleration magnitude minus gravity)
  - 4 FFT frequency bands: Band 0 (1-10 Hz), Band 1 (10-25 Hz), Band 2 (25-40 Hz), Band 3 (40-50 Hz)
  - Dominant vibration frequency (Hz)
  - Operating temperature (deg C)
- Baseline parameters are stored in non-volatile storage (NVS) via `Preferences` so they persist across power cycles.

### 2. Warning State Rules
The system enters WARNING if any condition holds for 3 consecutive windows (approx 3.8 seconds):
- Dynamic vibration RMS exceeds baseline mean + (2.0 * sigma) and is at least 1.3x baseline mean.
- Operating temperature rises by more than 8.0 deg C above baseline mean.
- Any FFT band energy exceeds 3.0x its baseline mean.
- Recovers back to NORMAL after 5 consecutive normal windows.

### 3. Critical State Rules (Latched)
The system trips the safety latch, cuts relay power, and turns on alarm outputs if:
- Dynamic vibration RMS exceeds 2.5x baseline mean and exceeds 0.70 m/s2 absolute minimum (requires 5 consecutive windows).
- Dynamic vibration RMS exceeds the absolute hard vibration limit (default 4.0 m/s2).
- Operating temperature exceeds the absolute hard temperature limit (default 60.0 deg C).
- Once latched, the fan cannot be turned on until an explicit reset command is issued and sensor readings have dropped back below critical thresholds.

## Serial and API Commands

Commands can be sent via HTTP POST to `/api/commands` or written as JSON lines over USB Serial:

| Command JSON | Description | Safety Behavior |
| :--- | :--- | :--- |
| `{"reset": true}` | Clears the critical safety latch | Clears latch only if current temperature and vibration are within safe limits. |
| `{"calibrate": true}` | Starts a new calibration cycle | Runs a 3-minute baseline learning sequence and saves new statistics to NVS. |
| `{"mode": "auto"}` | Sets fan control to automatic | Ignored while the appliance is latched. |
| `{"mode": "on"}` | Forces the fan ON | Ignored while the appliance is latched. |
| `{"mode": "off"}` | Forces the fan OFF | Ignored while the appliance is latched. |
| `{"limit_temp": 65}` | Updates hard temperature limit (deg C) | Stored in NVS. Clamped between 35 and 90 deg C. |
| `{"limit_vib": 5.0}` | Updates hard vibration limit (m/s2) | Stored in NVS. Clamped between 1.0 and 15.0 m/s2. |
| `{"mute_buzzer": true}` | Silences active buzzer | Keeps relay cutoff and red LED alarm active. |

## Getting Started

### 1. Flash the ESP32 Firmware
1. Open `monitor/monitor.ino` in Arduino IDE or VS Code with ESP32 board support installed.
2. Select target board: `ESP32 Dev Module`.
3. Ensure required libraries are installed:
   - `ArduinoJson` (v6)
   - `OneWire`
   - `DallasTemperature`
   - `Adafruit ADS1X15` (if using ADS1115 current sensor)
   - `arduinoFFT`
4. Connect the ESP32 via USB and upload the sketch.

### 2. Run the Offline Backend
The backend connects to the ESP32 over USB Serial, persists telemetry into SQLite, and hosts the API and dashboard.

```bash
cd backend
npm install
# Set SERIAL_PORT if different from COM8 (for example: COM9 on Windows or /dev/ttyUSB0 on Linux)
SERIAL_PORT=COM9 npm start
```

On Windows PowerShell:
```powershell
cd backend
$env:SERIAL_PORT="COM9"
npm start
```

Health check endpoint: `http://localhost:5000/api/health`

### 3. Build and View the Frontend Dashboard
The frontend is built with React, Vite, Recharts, and Lucide Icons.

```bash
cd frontend
npm install
npm run build
```

The built frontend files in `frontend/dist` are served directly by the backend at `http://localhost:5000`.

To run the Vite dev server with hot reload:
```bash
cd frontend
npm run dev
```

## Testing and Verification

1. **Baseline Calibration Test**:
   - Power on the ESP32 with the fan running normally.
   - Observe the `CALIBRATING` status and progress bar on the dashboard.
   - After 3 minutes, verify the state shifts to `NORMAL` with the green LED solid.
2. **Mild Imbalance Warning Test**:
   - Attach a small weight (such as a 10 mm tape strip) to one fan blade.
   - Observe vibration RMS and rotation frequency band rise.
   - Verify the yellow LED turns on, state changes to `WARNING`, and the fan keeps running.
   - Remove the weight and verify automatic recovery to `NORMAL` within 5 windows.
3. **Critical Safety Latch Test**:
   - Introduce severe vibration or gently loosen the fan mount.
   - Verify the state flips to `CRITICAL (LATCHED)`.
   - Verify the relay cuts fan power immediately, the red LED illuminates, and the buzzer sounds.
   - Send `{"mode": "on"}` and confirm the command is blocked by the latch.
   - Remove the disturbance and send `{"reset": true}` to restore normal operation.
4. **Sensor Fault Test**:
   - Disconnect the I2C SDA line (GPIO 21).
   - Verify immediate transition to `SENSOR_ERROR` and red LED indication.
   - Reconnect the wire and verify automatic recovery.
