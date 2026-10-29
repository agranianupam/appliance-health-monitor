const express = require('express');
const cors = require('cors');
const path = require('path');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const { initializeDatabase, getDatabase, saveDatabase } = require('./database');

const app = express();
const PORT = Number(process.env.PORT || 5000);
const SERIAL_PATH = process.env.SERIAL_PORT || 'COM8';
const SERIAL_BAUD = Number(process.env.SERIAL_BAUD || 115200);
const frontendDist = path.join(__dirname, '../frontend/dist');
app.use(cors());
app.use(express.json({ limit: '20kb' }));
app.use(express.static(frontendDist));

let serialPort = null;
let serialConnected = false;
let lastTelemetry = null;
let lastAck = null;
let lastSerialError = null;
let lastState = null;

function rowsToObjects(result) {
  if (!result.length) return [];
  const columns = result[0].columns;
  return result[0].values.map(values => Object.fromEntries(columns.map((c, i) => [c, values[i]])));
}
function telemetryRows(limit=100) {
  const db = getDatabase();
  const rows = rowsToObjects(db.exec(`SELECT id, device_id, health_state, created_at, telemetry_json FROM sensor_readings ORDER BY id DESC LIMIT ${Math.min(Math.max(Number(limit)||100,1),500)}`));
  return rows.reverse().map(r => ({ ...(safeJson(r.telemetry_json) || {}), id:r.id, device_id:r.device_id, state:r.health_state, created_at:r.created_at }));
}
function safeJson(s) { try { return s ? JSON.parse(s) : null; } catch { return null; } }
function storeTelemetry(data) {
  if (!data || (data.type && data.type !== 'telemetry')) return;
  if (typeof data.device_id !== 'string' || !data.device_id) return;
  if (!Number.isFinite(Number(data.temp_c)) || !Number.isFinite(Number(data.vib_rms))) {
    lastSerialError = 'Telemetry packet missing valid temperature or vibration'; return;
  }
  const current = data.current_a === null || data.current_a === undefined || data.current_a === '' ? null : Number(data.current_a);
  const state = String(data.state || 'SENSOR_ERROR');
  const db = getDatabase();
  db.run(`INSERT INTO sensor_readings (device_id, temperature_c, current_rms_a, vibration_rms_g, health_state, confidence, relay_state, accel_x, accel_y, accel_z, telemetry_json)
          VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`, [data.device_id, Number(data.temp_c), Number.isFinite(current) ? current : null, Number(data.vib_rms), state, null, data.fan_on ? 'ON' : 'OFF', null, null, null, JSON.stringify(data)]);
  if (state !== lastState && ['WARNING','CRITICAL','SENSOR_ERROR','CALIBRATING','LEARNING'].includes(state)) {
    db.run('INSERT INTO fault_events (device_id, health_state, message) VALUES (?, ?, ?)', [data.device_id, state, `${state}: ${data.state_reason || data.hint || 'Appliance state changed'}`]);
  }
  lastState = state;
  saveDatabase();
  lastTelemetry = { ...data, online:true, lastSeen:Date.now() };
  lastSerialError = null;
}
function openSerial() {
  try {
    serialPort = new SerialPort({ path: SERIAL_PATH, baudRate: SERIAL_BAUD, autoOpen:false });
    const parser = serialPort.pipe(new ReadlineParser({ delimiter:'\n' }));
    serialPort.on('open', () => { serialConnected = true; lastSerialError = null; console.log(`Serial connected: ${SERIAL_PATH} @ ${SERIAL_BAUD}`); });
    serialPort.on('close', () => { serialConnected = false; console.log('Serial port closed'); });
    serialPort.on('error', e => { serialConnected = false; lastSerialError = e.message; console.error('Serial error:', e.message); });
    parser.on('data', line => {
      const trimmed = String(line).trim();
      if (!trimmed.startsWith('{')) return; // ignore boot/debug lines
      try {
        const packet = JSON.parse(trimmed);
        if (packet.type === 'ack') lastAck = { ...packet, received_at:Date.now() };
        else if (!packet.type || packet.type === 'telemetry') storeTelemetry(packet);
      } catch { /* partial/debug output is ignored */ }
    });
    serialPort.open(err => { if (err) { serialConnected = false; lastSerialError = err.message; console.error(`Could not open ${SERIAL_PATH}: ${err.message}`); } });
  } catch (e) { serialConnected = false; lastSerialError = e.message; console.error('Serial setup failed:', e.message); }
}
app.get('/api/health', (_req,res) => res.json({status:'ok', service:'appliance-health-api', offline:true}));
app.get('/api/status', (_req,res) => res.json({serial_connected:serialConnected, serial_path:SERIAL_PATH, baud_rate:SERIAL_BAUD, last_error:lastSerialError, last_seen:lastTelemetry?.lastSeen || null}));
app.get('/api/latest', (_req,res) => {
  if (lastTelemetry) return res.json(lastTelemetry);
  const rows = telemetryRows(1); res.json(rows[0] || null);
});
app.get('/api/history', (req,res) => res.json(telemetryRows(req.query.limit || 100)));
app.get('/api/events', (_req,res) => {
  const db=getDatabase();
  res.json(rowsToObjects(db.exec('SELECT id, device_id, health_state AS state, message, created_at FROM fault_events ORDER BY id DESC LIMIT 100')));
});
app.get('/api/ack', (_req,res) => res.json(lastAck));
app.post('/api/commands', (req,res) => {
  if (!serialConnected || !serialPort?.isOpen) return res.status(503).json({error:'ESP32 serial connection is not available', details:lastSerialError});
  const allowed = ['mode','reset','calibrate','cal_current','limit_temp','limit_vib','limit_curr','mute_buzzer'];
  const command = req.body || {};
  if (!Object.keys(command).some(k => allowed.includes(k))) return res.status(400).json({error:'Unsupported command'});
  const sendTime = Date.now();
  serialPort.write(JSON.stringify(command) + '\n', err => {
    if (err) return res.status(500).json({error:'Failed to send command', details:err.message});
    const interval = setInterval(() => {
      if (lastAck && lastAck.received_at >= sendTime) {
        clearInterval(interval);
        clearTimeout(timeout);
        return res.json({success:true, command, ack:lastAck});
      }
    }, 20);
    const timeout = setTimeout(() => {
      clearInterval(interval);
      res.json({success:true, command, ack:null});
    }, 600);
  });
});
app.use((req,res,next) => { if(req.path.startsWith('/api/')) return next(); res.sendFile(path.join(frontendDist,'index.html')); });
async function start() {
  await initializeDatabase();
  const db=getDatabase();
  const columns=rowsToObjects(db.exec('PRAGMA table_info(sensor_readings)')).map(r=>r.name);
  for (const [name,type] of [['accel_x','REAL'],['accel_y','REAL'],['accel_z','REAL'],['telemetry_json','TEXT']]) if(!columns.includes(name)) db.run(`ALTER TABLE sensor_readings ADD COLUMN ${name} ${type}`);
  saveDatabase();
  app.listen(PORT,'127.0.0.1',() => console.log(`Local dashboard API: http://localhost:${PORT}/api/health`));
  openSerial();
}
start().catch(e => { console.error('Backend startup failed:',e); process.exit(1); });
