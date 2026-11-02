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

## Dependencies to Download

### 1. Software and Runtimes
- **Node.js**: Version 18.x or 20.x LTS with npm (download from https://nodejs.org).
- **Arduino IDE**: Version 2.x or VS Code with Arduino Extension (download from https://www.arduino.cc/en/software).
- **USB Driver**: CH340 or CP2102 driver (matching your ESP32 board).

### 2. ESP32 Board Package (Arduino IDE)
1. In Arduino IDE, open **File > Preferences**.
2. Add this URL to **Additional Boards Manager URLs**:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools > Board > Boards Manager**, search for `esp32` by Espressif Systems, and click **Install**.

### 3. Arduino Libraries
Open **Tools > Manage Libraries** in Arduino IDE and install:
- `ArduinoJson` (version 6.21.x by Benoit Blanchon)
- `OneWire` (by Paul Stoffregen)
- `DallasTemperature` (by Miles Burton)
- `Adafruit ADS1X15` (by Adafruit, required if current sensor is enabled)
- `PubSubClient` (by Nick O'Leary)

### 4. Backend Dependencies
The backend uses standard npm packages defined in `backend/package.json`:
- `express` (HTTP server and static hosting)
- `cors` (Cross-origin resource sharing middleware)
- `serialport` and `@serialport/parser-readline` (USB serial communication with ESP32)
- `sql.js` (Pure WebAssembly SQLite database engine)

### 5. Frontend Dependencies
The frontend dashboard uses packages defined in `frontend/package.json`:
- `react` and `react-dom` (Core UI library)
- `recharts` (Time series and sensor trend charts)
- `lucide-react` (Dashboard icons)
- `vite` and `@vitejs/plugin-react` (Fast build tool and dev server)

## Instructions to Run the Project

Follow these steps to run the complete end-to-end system:

### Step 1: Connect and Flash the ESP32
1. Connect your ESP32 to your computer using a micro-USB or USB-C data cable.
2. Open `monitor/monitor.ino` in Arduino IDE.
3. Select **Tools > Board > ESP32 Arduino > ESP32 Dev Module**.
4. Select your COM port under **Tools > Port** (for example `COM9` on Windows or `/dev/ttyUSB0` on Linux).
5. Click **Upload** and wait for upload completion.
6. Verify in the Serial Monitor (set to 115200 baud) that the board outputs:
   `{"type":"status","status":"serial_ready","device_id":"fan-01"}`
7. Close the Serial Monitor so the backend can access the COM port.

### Step 2: Install and Run the Backend Server
The backend reads serial packets from the ESP32, writes them to SQLite, and provides API endpoints.

On Windows (PowerShell):
```powershell
cd backend
npm install
$env:SERIAL_PORT="COM9"
npm start
```

On Linux or macOS:
```bash
cd backend
npm install
SERIAL_PORT=/dev/ttyUSB0 npm start
```

You should see:
```text
Database loaded successfully
Local dashboard API: http://localhost:5000/api/health
Serial connected: COM9 @ 115200
```

### Step 3: Install and Build the Frontend Dashboard

Option A: Production Build (Recommended)
Build the frontend static files once so the Node.js backend serves both the API and dashboard on a single port (5000):
```bash
cd frontend
npm install
npm run build
```
Once built, open `http://localhost:5000` in any web browser.

Option B: Development Mode (Hot Reload)
To run the React development server with hot reload:
```bash
cd frontend
npm install
npm run dev
```
Open `http://localhost:5173` in your browser (it proxies requests to port 5000).

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
