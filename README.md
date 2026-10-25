# APEX — IoT Appliance Health Monitor

Local prototype stack: ESP32 firmware → Express API → SQLite → React dashboard.

## Requirements
- Node.js LTS
- VS Code
- Arduino IDE and ESP32 board support
- ESP32 and the sensors described in `firmware/appliance_monitor.ino`

## 1. Start backend (PowerShell terminal 1)
```powershell
cd backend
npm install
npm start
```
Check `http://localhost:5000/api/health`.

## 2. Start dashboard (PowerShell terminal 2)
```powershell
cd frontend
npm install
npm run dev
```
Open the URL Vite prints, normally `http://localhost:5173`.

## 3. Software-only API test
Run while backend is running:
```powershell
$body = @{
  device_id = 'TEST-01'
  temperature_c = 32.5
  current_rms_a = 0.4
  vibration_rms_g = 0.08
  health_state = 'Normal'
  confidence = 0.9
  relay_state = 'ON'
} | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri 'http://localhost:5000/api/readings' -ContentType 'application/json' -Body $body
```
This is test data, not real sensor data. It should appear on the dashboard as a software pipeline test.

## 4. Connect ESP32
- Install the libraries listed at the top of the firmware sketch.
- Set Wi-Fi credentials and change `API_URL` to your laptop IPv4 address from `ipconfig`.
- Ensure the laptop and ESP32 are on the same reachable network and Windows Firewall allows port 5000.
- Wire only according to your exact module pinouts and voltage limits.
- Calibrate the ACS712 before using its readings. The firmware intentionally reports health `Unknown` until a validated TinyML model is added.

## API endpoints
- `GET /api/health`
- `POST /api/readings`
- `GET /api/latest`
- `GET /api/history?limit=100`
- `GET /api/events`

## Important limitations
- The included firmware is a starter, not a complete safety-certified appliance controller.
- It does not yet run TinyML or implement relay shutdown. Keep critical protection local to the ESP32 and test with a low-voltage DC load under appropriate supervision.
- Current calibration and sampling depend on the exact ACS712 board, load, and signal type. Do not connect exposed mains wiring to a breadboard.
- The Google Fonts import is optional; the dashboard still works without it if offline.
