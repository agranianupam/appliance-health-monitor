# APEX - Adaptive Statistical Appliance Health Monitor

A low-cost, edge-intelligent predictive maintenance monitor for small rotating appliances (5V DC fan) built on the ESP32. It uses statistical baseline calibration, 4-band FFT vibration analysis, and a safety latch, connected to a real-time React/MQTT WebSockets dashboard.

---

## 1. System Architecture

```
               [ 100 Hz Micros Timing ]
           MPU6050 (Clone 0x74) + DS18B20 + ADS1115
                            |
               +------------+------------+
               |    ESP32 CORE 1 TASK    | (Priority 2)
               |  * 128-sample window    |
               |  * FFT & 4-Band Energy  |
               |  * Baseline Calibration |
               |  * Statistical Rules    |
               |  * Safety Latch Logic   |
               |  * Actuators (Relay/LED)|
               +------------+------------+
                            | Shared Telemetry Snapshot
               +------------+------------+
               |    ESP32 CORE 0 TASK    | (Priority 1)
               |  * WiFi & WiFiManager   |
               |  * TLS MQTT (PubSub)    |
               |  * RAM Ring Buffer (10m)|
               |  * ArduinoOTA Updates   |
               |  * Telegram Alert Bot   |
               +------------+------------+
                            | (WSS / WebSockets)
             HiveMQ Cloud / MQTT Broker (Port 8884)
                            |
               +------------+------------+
               |   REACT VITE DASHBOARD  | (Browser-Direct)
               |  * Radial Gauges        |
               |  * State Badges         |
               |  * Latch Banner & Reset |
               |  * Calibration Progress |
               |  * Diagnostic Hints     |
               |  * Time to Critical TTC |
               +-------------------------+
```

---

## 2. Hardware Wiring Reference

| Peripheral | ESP32 GPIO | Description / Configuration |
| :--- | :--- | :--- |
| **MPU6050 (Clone)** | SDA = `GPIO 21`<br>SCL = `GPIO 22` | Clone `WHO_AM_I = 0x74`. Read via direct I2C registers: Wake (`0x6B = 0x00`), DLPF ~21 Hz (`0x1A = 0x04`), Range +/-8g (`0x1C = 0x10`), 400 kHz clock. |
| **DS18B20 Probe** | `GPIO 4` | 4.7k pull-up to 3.3V. Non-blocking conversion (spanned across 128-sample window). |
| **ADS1115 (A0)** | SDA = `GPIO 21`<br>SCL = `GPIO 22` | Address `0x48`, `GAIN_ONE` (+/-4.096V). Reads ACS712 via 22k/10k divider (ratio 10/32). Optional via `#define USE_CURRENT`. |
| **Relay Module** | `GPIO 26` | `HIGH` = Fan ON (Energized), `LOW` = Fan OFF (Power Cut). Verified on DevKit. |
| **Green LED** | `GPIO 32` | **NORMAL & CALIBRATING** indicator (330 ohm resistor). |
| **Yellow LED** | `GPIO 33` | **WARNING** indicator (330 ohm resistor). |
| **Red LED** | `GPIO 25` | **CRITICAL (LATCHED)** & **SENSOR ERROR** indicator. |
| **Active Buzzer** | `GPIO 27` | **Active HIGH** (sounds during CRITICAL unless muted remotely). |

---

## 3. Statistical Baseline Calibration & Decision Rules

### Baseline Calibration (First 3 Minutes)
On first boot, or whenever a `{"calibrate": true}` command is sent:
- The monitor enters `CALIBRATING` state for 3 minutes (140 windows of 128 samples).
- The fan runs normally while the system accumulates the mean and standard deviation for:
  - Dynamic vibration RMS (acceleration magnitude minus gravity)
  - 4 FFT band energies: Band 0 (1-10 Hz), Band 1 (10-25 Hz), Band 2 (25-40 Hz), Band 3 (40-50 Hz)
  - Dominant vibration frequency (Hz)
  - Temperature (deg C)
- At the end of 3 minutes, values are saved to NVS (`Preferences`) so they persist across power cycles.
- Hard safety limits remain active during calibration.

### Anomaly Rules & Threshold Constants
```cpp
const float WARN_VIB_SIGMA_MULT = 3.0f;
const float WARN_VIB_RATIO_MIN  = 1.5f;
const float WARN_TEMP_RISE_DEG  = 8.0f;
const float WARN_BAND_RATIO     = 3.0f;
const float CRIT_VIB_RATIO      = 3.0f;
const float CRIT_VIB_ABS_MIN    = 1.5f;
```

- **WARNING State:**
  - Dynamic vibration RMS exceeds baseline mean + 3 standard deviations (and at least 1.5x baseline mean)
  - OR temperature rises more than 8 deg C above baseline mean
  - OR any FFT band energy exceeds 3x its baseline mean
  - Requires 2 consecutive windows to enter WARNING. Requires 5 consecutive normal windows to return to NORMAL.

- **CRITICAL State (LATCHED):**
  - Dynamic vibration RMS exceeds 3x baseline mean (and exceeds 1.5 m/s2 absolute minimum)
  - OR temperature exceeds the hard temperature limit (default 60 deg C)
  - OR vibration exceeds the hard vibration limit (default 4.0 m/s2)
  - Requires 2 consecutive windows to trigger (or 1 window if hard limits are violated).

---

## 4. State & Actuator Behavior Table

| State | Relay Output | Green LED | Yellow LED | Red LED | Buzzer | Latch Behavior |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **NORMAL** | ON (Energized) | ON | OFF | OFF | OFF | Unlatched |
| **CALIBRATING** | ON (Energized) | ON | OFF | OFF | OFF | Unlatched (Progress bar on UI) |
| **WARNING** | ON (Energized) | OFF | ON | OFF | OFF | Unlatched (Warning on UI) |
| **CRITICAL** | **OFF (Power Cut)** | OFF | OFF | **ON** | **ON** | **LATCHED** (Requires `{"reset": true}`) |
| **SENSOR_ERROR**| ON (Energized) | OFF | OFF | **ON** | OFF | Unlatched (Auto-recovers when wired) |

---

## 5. MQTT Remote Commands

Commands are published as JSON to topic `<prefix>/<device_id>/command`:

| Command Payload | Purpose | Safety Rules |
| :--- | :--- | :--- |
| `{"reset": true}` | Clear critical safety latch | Clears latch only if sensor values are below critical limits. |
| `{"calibrate": true}` | Calibrate baseline | Triggers a fresh 3-minute baseline calibration cycle. |
| `{"mode": "auto"}` | Set fan mode to auto | Ignored if device is currently latched. |
| `{"mode": "on"}` | Force fan ON | Ignored if device is currently latched. |
| `{"mode": "off"}` | Force fan OFF | Ignored if device is currently latched. |
| `{"limit_temp": 65}` | Update hard temperature limit | Stored in NVS. Clamped between 35 and 90 deg C. |
| `{"limit_vib": 5.0}` | Update hard vibration limit | Stored in NVS. Clamped between 1.0 and 15.0 m/s2. |
| `{"mute_buzzer": true}`| Silence buzzer alarm | Keeps Red LED and Relay power cut active. |

---

## 6. Hardware Verification & Testing Checklist

### Test 1: Baseline Calibration
1. Power on the ESP32 with the fan firmly mounted and running normally.
2. The dashboard state displays `CALIBRATING` with a progress bar (0 to 100%).
3. Relay stays ON, Green LED stays ON.
4. After 3 minutes, state transitions automatically to `NORMAL`.
5. Reboot the ESP32. Verify in Serial Monitor that the baseline is loaded from NVS without restarting calibration.

### Test 2: Warning Detection (Mild Imbalance)
1. Add a small piece of tape (~10 mm) to one blade tip of the fan.
2. Observe telemetry: vibration RMS and Band 2 (25-40 Hz rotation band) increase.
3. Yellow LED turns ON; dashboard displays `WARNING` and diagnostic hint:
   `energy rising at rotation frequency: possible imbalance`.
4. Relay remains ON.
5. Remove the tape. After 5 consecutive normal windows, state returns to `NORMAL` and Green LED turns ON.

### Test 3: Critical State & Safety Latch
1. Apply heavier tape or gently loosen the fan mounting bracket to create strong vibration.
2. When vibration exceeds 3x baseline:
   - State flips to `CRITICAL (LATCHED)`.
   - Relay turns OFF immediately, cutting fan power.
   - Red LED turns ON, Buzzer sounds.
   - Dashboard displays red banner: `LATCHED - FAN POWER CUT`.
3. Try sending manual command `{"mode": "on"}` from the dashboard. Verify that manual commands are blocked and the fan does not spin.
4. Fix the fan, then click `RESET LATCH` (`{"reset": true}`).
5. Verify latch clears, Relay re-energizes, and Green LED turns ON.

### Test 4: Offline WiFi Resilience
1. While the fan is running in `NORMAL` state, power down or disconnect the Wi-Fi router.
2. Observe the hardware:
   - 100 Hz sampling on Core 1 continues with zero timing disruption.
   - Green LED stays solid; relay stays energized.
3. Tap or shake the fan: verify Yellow LED (Warning) or Red LED + Relay cutoff (Critical) triggers locally without network connection.
4. Reconnect Wi-Fi: verify buffered readings are transmitted to the broker.

### Test 5: Sensor Unplug / Fault Test
1. Unplug the I2C SDA wire (GPIO 21).
2. The firmware immediately detects I2C failure and transitions to `SENSOR_ERROR`.
3. Relay turns OFF, Red LED turns ON, Buzzer stays OFF.
4. Plug the SDA wire back in: verify the firmware recovers without requiring a manual reset.
