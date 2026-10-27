import React, { useEffect, useState, useRef } from 'react';
import {
  Activity,
  AlertTriangle,
  CheckCircle2,
  Clock3,
  Cpu,
  Radio,
  ShieldCheck,
  ShieldAlert,
  Thermometer,
  Waves,
  Zap,
  Sliders,
  BellOff,
  Bell,
  Power,
  RotateCcw,
  Layers,
  Unlock,
  Info
} from 'lucide-react';
import {
  ResponsiveContainer,
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip
} from 'recharts';

const MQTT_BROKER = import.meta.env.VITE_MQTT_BROKER || 'broker.hivemq.com';
const MQTT_PORT = import.meta.env.VITE_MQTT_PORT || '8000';
const MQTT_PROTOCOL = import.meta.env.VITE_MQTT_PROTOCOL || 'ws';
const MQTT_PREFIX = import.meta.env.VITE_MQTT_TOPIC_PREFIX || 'fanmon_x7k29q4';
const MQTT_USER = import.meta.env.VITE_MQTT_USERNAME || '';
const MQTT_PASS = import.meta.env.VITE_MQTT_PASSWORD || '';

function CircularGauge({ value, min = 0, max = 100, unit = '', label, color = '#38c8ee', threshold = null }) {
  const radius = 42;
  const circumference = 2 * Math.PI * radius;
  const hasValue = Number.isFinite(value);
  const clampedVal = hasValue ? Math.min(Math.max(value, min), max) : min;
  const percent = hasValue ? (clampedVal - min) / (max - min) : 0;
  const strokeDashoffset = circumference - percent * circumference * 0.75;

  return (
    <div className="gauge-box">
      <div className="gauge-svg-wrap">
        <svg viewBox="0 0 100 100" className="gauge-svg">
          <circle
            cx="50" cy="50" r={radius}
            className="gauge-bg"
            strokeDasharray={`${circumference * 0.75} ${circumference * 0.25}`}
            strokeDashoffset={0}
            transform="rotate(135 50 50)"
          />
          <circle
            cx="50" cy="50" r={radius}
            className="gauge-val"
            stroke={hasValue ? color : '#334155'}
            strokeDasharray={`${circumference * 0.75} ${circumference * 0.25}`}
            strokeDashoffset={strokeDashoffset}
            transform="rotate(135 50 50)"
          />
        </svg>
        <div className="gauge-center">
          <span className="gauge-num" style={{ color: hasValue ? color : '#64748b' }}>
            {hasValue ? value : '-'}
          </span>
          {hasValue && <span className="gauge-unit">{unit}</span>}
        </div>
      </div>
      <div className="gauge-info">
        <strong>{label}</strong>
        {threshold && <small>Limit: {threshold}{unit}</small>}
      </div>
    </div>
  );
}

export default function App() {
  const [mqttClient, setMqttClient] = useState(null);
  const [brokerConnected, setBrokerConnected] = useState(false);
  const [discoveredDevices, setDiscoveredDevices] = useState(['fan-01']);
  const [activeDeviceId, setActiveDeviceId] = useState('fan-01');

  const [deviceData, setDeviceData] = useState({});
  const [telemetryHistory, setTelemetryHistory] = useState([]);
  const [events, setEvents] = useState([]);
  const [ackNotice, setAckNotice] = useState(null);

  const [tempLimitInput, setTempLimitInput] = useState(60);
  const [vibLimitInput, setVibLimitInput] = useState(4.0);
  const [currLimitInput, setCurrLimitInput] = useState(1.0);
  const [isApplyingLimits, setIsApplyingLimits] = useState(false);
  const [buzzerMuted, setBuzzerMuted] = useState(false);

  const [now, setNow] = useState(Date.now());
  const lastStateMapRef = useRef({});

  useEffect(() => {
    const timer = setInterval(() => {
      setNow(Date.now());
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  const devEntry = deviceData[activeDeviceId];
  const lastSeen = devEntry?.lastSeen;
  const isFresh = Boolean(lastSeen && (now - lastSeen <= 10000) && devEntry?.online !== false);

  const currentTelemetry = (devEntry && isFresh) ? devEntry : {
    device_id: activeDeviceId,
    temp_c: null,
    vib_rms: null,
    current_a: null,
    state: isFresh ? (devEntry?.state || 'NORMAL') : 'DEVICE OFFLINE',
    state_reason: 'none',
    reason_val: null,
    reason_limit: null,
    latched: devEntry?.latched || false,
    baseline_ready: devEntry?.baseline_ready || false,
    learning_pct: devEntry?.learning_pct || 0,
    dominant_freq_hz: null,
    rise_band: 0,
    hint: isFresh ? (devEntry?.hint || '') : 'Device offline: No real-time telemetry received',
    health_score: null,
    ttc_min: null,
    fan_on: devEntry?.fan_on || false,
    mode: devEntry?.mode || 'auto',
    rssi: null,
    uptime_s: null,
    online: isFresh,
    lastSeen: lastSeen
  };

  useEffect(() => {
    if (!window.mqtt) {
      return;
    }

    const wsUrl = MQTT_PROTOCOL + "://" + MQTT_BROKER + ":" + MQTT_PORT + "/mqtt";
    const options = {
      clientId: `dash_${Math.random().toString(16).substring(2, 8)}`,
      clean: true,
      connectTimeout: 5000,
      reconnectPeriod: 3000
    };
    if (MQTT_USER && MQTT_USER.trim()) {
      options.username = MQTT_USER;
      options.password = MQTT_PASS;
    }

    const client = window.mqtt.connect(wsUrl, options);

    client.on('connect', () => {
      setBrokerConnected(true);
      client.subscribe(`${MQTT_PREFIX}/+/telemetry`);
      client.subscribe(`${MQTT_PREFIX}/+/status`);
      client.subscribe(`${MQTT_PREFIX}/+/ack`);
    });

    client.on('error', () => {
    });

    client.on('close', () => {
      setBrokerConnected(false);
    });

    client.on('message', (topic, payload) => {
      try {
        const msgStr = payload.toString();
        const data = JSON.parse(msgStr);
        const parts = topic.split('/');
        const devId = parts[1];
        const subtopic = parts[2];

        setBrokerConnected(true);
        setDiscoveredDevices(prev => prev.includes(devId) ? prev : [...prev, devId]);

        if (subtopic === 'telemetry') {
          const nowTime = new Date().toLocaleTimeString();
          setDeviceData(prev => ({
            ...prev,
            [devId]: {
              ...data,
              online: true,
              lastSeen: Date.now()
            }
          }));

          if (devId === activeDeviceId) {
            setTelemetryHistory(prev => [
              ...prev.slice(-45),
              {
                time: nowTime,
                temp_c: data.temp_c,
                vib_rms: data.vib_rms,
                health_score: data.health_score
              }
            ]);
          }

          const prevState = lastStateMapRef.current[devId];
          if (data.state && data.state !== prevState) {
            lastStateMapRef.current[devId] = data.state;
            if (data.state === 'CRITICAL' || data.state === 'WARNING' || data.state === 'SENSOR_ERROR' || data.state === 'CALIBRATING' || data.state === 'LEARNING') {
              const reasonStr = data.state_reason || (data.state === 'CALIBRATING' || data.state === 'LEARNING' ? 'baseline calibration triggered' : 'threshold exceeded');
              const detailStr = (data.reason_val !== undefined && data.reason_limit !== undefined && data.reason_limit !== 0)
                ? ` (${data.reason_val} vs limit ${data.reason_limit})`
                : '';
              setEvents(prev => [
                {
                  id: Date.now(),
                  device_id: devId,
                  state: data.state === 'LEARNING' ? 'CALIBRATING' : data.state,
                  reason: reasonStr,
                  time: nowTime,
                  message: `${data.state === 'LEARNING' ? 'CALIBRATING' : data.state}: ${reasonStr}${detailStr}`
                },
                ...prev.slice(0, 29)
              ]);
            }
          }
        } else if (subtopic === 'status') {
          const isOnline = (msgStr.trim().toLowerCase() === 'online');
          setDeviceData(prev => ({
            ...prev,
            [devId]: {
              ...(prev[devId] || {}),
              online: isOnline,
              lastSeen: isOnline ? Date.now() : prev[devId]?.lastSeen
            }
          }));
        } else if (subtopic === 'ack') {
          setAckNotice(`Acknowledged by ${devId} at ${new Date().toLocaleTimeString()}`);
          setTimeout(() => setAckNotice(null), 4000);
        }
      } catch (err) {
      }
    });

    setMqttClient(client);

    return () => {
      client.end();
    };
  }, [activeDeviceId]);

  const sendDeviceCommand = (cmdObj) => {
    if (!mqttClient || !isMqttConnected) {
      alert("MQTT broker disconnected");
      return;
    }
    const topic = `${MQTT_PREFIX}/${activeDeviceId}/command`;
    mqttClient.publish(topic, JSON.stringify(cmdObj));
  };

  const handleSetFanMode = (mode) => {
    sendDeviceCommand({ mode });
  };

  const handleResetLatch = () => {
    sendDeviceCommand({ reset: true });
  };

  const handleCalibrateBaseline = () => {
    if (isLatched) {
      setAckNotice("Device is latched: Reset the latch first");
      setTimeout(() => setAckNotice(null), 3000);
      return;
    }
    if (window.confirm("Start 3-minute baseline calibration? Ensure the fan is firmly mounted and operating normally.")) {
      sendDeviceCommand({ calibrate: true });
    }
  };

  const handleApplyLimits = () => {
    setIsApplyingLimits(true);
    sendDeviceCommand({
      limit_temp: parseFloat(tempLimitInput),
      limit_vib: parseFloat(vibLimitInput),
      limit_curr: parseFloat(currLimitInput)
    });
    setTimeout(() => setIsApplyingLimits(false), 1200);
  };

  const handleToggleBuzzer = () => {
    const next = !buzzerMuted;
    setBuzzerMuted(next);
    sendDeviceCommand({ mute_buzzer: next });
  };

  const state = isFresh ? (currentTelemetry.state || 'NORMAL') : 'DEVICE OFFLINE';
  const isNormal = state === 'NORMAL';
  const isWarning = state === 'WARNING';
  const isCritical = state === 'CRITICAL';
  const isCalibrating = state === 'CALIBRATING' || state === 'LEARNING';
  const isLearning = isCalibrating;
  const isSensorError = state === 'SENSOR_ERROR';
  const isOffline = !isFresh || state === 'DEVICE OFFLINE';
  const isLatched = currentTelemetry.latched;
  const isMqttConnected = brokerConnected || Boolean(mqttClient && mqttClient.connected) || isFresh;

  let badgeColor = '#65d9a9';
  let badgeTone = 'good';
  if (isOffline) { badgeColor = '#94a3b8'; badgeTone = 'offline'; }
  else if (isWarning) { badgeColor = '#f0c16d'; badgeTone = 'warn'; }
  else if (isCritical || isLatched) { badgeColor = '#ff777e'; badgeTone = 'critical'; }
  else if (isCalibrating) { badgeColor = '#50b8e7'; badgeTone = 'calibrating'; }
  else if (isSensorError) { badgeColor = '#ff9f68'; badgeTone = 'critical'; }

  const bandNames = ["1-10 Hz (Looseness)", "10-25 Hz (Harmonics)", "25-40 Hz (1X Rotation)", "40-50 Hz (High Freq)"];

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark">
            <Cpu size={22} />
          </div>
          <div>
            <strong>APEX HEALTH</strong>
            <span>ADAPTIVE STATISTICAL MONITOR</span>
          </div>
        </div>

        <p className="side-section">TARGET APPLIANCE</p>
        <div className="device-selector-wrap">
          <Layers size={14} className="device-select-icon" />
          <select
            value={activeDeviceId}
            onChange={(e) => setActiveDeviceId(e.target.value)}
            className="device-dropdown"
          >
            {discoveredDevices.map(id => (
              <option key={id} value={id}>{id}</option>
            ))}
          </select>
        </div>

        <p className="side-section" style={{ marginTop: '24px' }}>TELEMETRY STATUS</p>
        <div className="sidebar-stat-box">
          <div className="stat-line">
            <span>Broker:</span>
            <strong>{MQTT_BROKER}</strong>
          </div>
          <div className="stat-line">
            <span>Transport:</span>
            <strong>{MQTT_PROTOCOL.toUpperCase()} / {MQTT_PORT}</strong>
          </div>
          <div className="stat-line">
            <span>Baseline:</span>
            <strong style={{ color: currentTelemetry.baseline_ready ? '#4fe3a7' : '#f0c16d' }}>
              {currentTelemetry.baseline_ready ? 'Calibrated' : 'Calibrating'}
            </strong>
          </div>
          <div className="stat-line">
            <span>Device Link:</span>
            <span className={`live-pill ${isFresh ? 'live' : 'offline'}`}>
              <span className="live-dot" />
              {isFresh ? 'LIVE' : 'OFFLINE'}
            </span>
          </div>
        </div>

        <div className="sidebar-bottom">
          <div className="device-mini">
            <span className={`connection-dot ${isMqttConnected ? 'online' : ''}`} />
            <div>
              <strong>MQTT WebSockets</strong>
              <small>{isMqttConnected ? 'Connected & Subscribed' : 'Reconnecting...'}</small>
            </div>
          </div>
          <p>FIRMWARE v3.0.0 (STATISTICAL)</p>
        </div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div>
            <div className="eyebrow">STATISTICAL ADAPTIVE MONITOR</div>
            <h1>{activeDeviceId} Appliance Monitor</h1>
            <p className="subtitle">Real-time baseline deviation analysis with automated safety latch</p>
          </div>

          <div className="topbar-right">
            <div className={`connection-pill ${isMqttConnected ? 'connected' : ''}`}>
              <span /> {isMqttConnected ? 'MQTT CONNECTED' : 'DISCONNECTED'}
            </div>
            <div className={`live-pill ${isFresh ? 'live' : 'offline'}`}>
              <span className="live-dot" /> {isFresh ? 'LIVE TELEMETRY' : 'DEVICE OFFLINE'}
            </div>
            {ackNotice && <div className="ack-banner">{ackNotice}</div>}
          </div>
        </header>

        {isLatched && (
          <div className="latch-alert-banner">
            <div className="latch-banner-content">
              <ShieldAlert size={28} className="pulse-icon" />
              <div>
                <strong>LATCHED - FAN POWER CUT</strong>
                <span>Critical threshold exceeded. Power is cut and relay is locked open until cleared.</span>
              </div>
            </div>
            <button className="btn-reset-latch" onClick={handleResetLatch}>
              <Unlock size={16} /> RESET LATCH
            </button>
          </div>
        )}

        {isCalibrating && (
          <div className="calibrating-banner">
            <div className="calibrating-banner-top">
              <span><RotateCcw size={14} className="spin-icon" /> CALIBRATING BASELINE IN PROGRESS</span>
              <strong>{currentTelemetry.learning_pct || 0}%</strong>
            </div>
            <div className="progress-bar-track">
              <div
                className="progress-bar-fill"
                style={{ width: `${currentTelemetry.learning_pct || 0}%` }}
              />
            </div>
            <small>Recording 3 minutes of normal operational vibration and thermal dynamics.</small>
          </div>
        )}

        <section className="state-hero-grid">
          <div className={`state-hero-badge ${badgeTone}`}>
            <div className="badge-header">
              {isOffline ? <AlertTriangle size={36} /> :
               (isCritical || isLatched) ? <ShieldAlert size={36} className="pulse-icon" /> :
               isWarning ? <AlertTriangle size={36} /> :
               isCalibrating ? <RotateCcw size={36} className="spin-icon" /> :
               <ShieldCheck size={36} />}
              <div>
                <span className="badge-sub">MACHINE HEALTH STATUS</span>
                <h2>{isOffline ? 'DEVICE OFFLINE' : (isLatched ? 'CRITICAL (LATCHED)' : (state === 'LEARNING' ? 'CALIBRATING' : state))}</h2>
              </div>
            </div>
            <p className="state-desc">
              {isOffline && "No real-time telemetry received from appliance within the last 10 seconds."}
              {!isOffline && isNormal && "Appliance is operating within healthy vibrational and thermal bounds."}
              {!isOffline && isWarning && `Statistical anomaly detected: ${currentTelemetry.state_reason || 'deviation from baseline'}.`}
              {!isOffline && isCritical && `CRITICAL ANOMALY: ${currentTelemetry.state_reason || 'threshold exceeded'}. Relay shut off.`}
              {!isOffline && isCalibrating && "Building dynamic statistical baseline from operational sensors."}
              {!isOffline && isSensorError && "Sensor disconnected or invalid data received. Check I2C wiring."}
            </p>
            <div className="state-meta-row">
              <span>Dominant Freq: <strong>{isFresh && currentTelemetry.dominant_freq_hz ? `${currentTelemetry.dominant_freq_hz.toFixed(1)} Hz` : '-'}</strong></span>
              <span>Primary Rise: <strong>{isFresh ? bandNames[currentTelemetry.rise_band || 0] : '-'}</strong></span>
              <span>Relay: <strong style={{ color: (isFresh && currentTelemetry.fan_on) ? '#52e1a8' : '#ff7e85' }}>
                {!isFresh ? 'UNKNOWN' : (currentTelemetry.fan_on ? 'ENERGIZED (ON)' : 'POWER CUT (OFF)')}
              </strong></span>
            </div>
          </div>

          <div className="health-score-hero">
            <div className="health-hero-gauge">
              <CircularGauge
                value={isFresh && Number.isFinite(currentTelemetry.health_score) ? Math.round(currentTelemetry.health_score) : null}
                label="HEALTH SCORE"
                unit="%"
                color={badgeColor}
              />
            </div>
            <div className="ttc-box">
              <span className="ttc-label"><Clock3 size={14} /> TIME TO CRITICAL</span>
              <strong className="ttc-value">
                {isFresh && currentTelemetry.ttc_min && currentTelemetry.ttc_min > 0
                  ? `${currentTelemetry.ttc_min.toFixed(1)} min`
                  : (isFresh ? 'Nominal' : '-')}
              </strong>
              <small>{isFresh ? (currentTelemetry.ttc_min ? 'Projected via linear regression trend' : 'No upward runaway trend') : 'Awaiting real telemetry'}</small>
            </div>
          </div>
        </section>

        <section className="anomaly-hint-card">
          <div className="hint-icon-wrap">
            <Info size={20} />
          </div>
          <div className="hint-body">
            <span className="hint-label">VIBRATION DIAGNOSTIC HINT</span>
            <p className="hint-text">
              {isFresh ? (currentTelemetry.hint || "nominal baseline vibration") : "Device offline: No real-time telemetry received"}
            </p>
          </div>
          <div className="hint-freq-badge">
            <span>Peak: {isFresh && currentTelemetry.dominant_freq_hz ? `${currentTelemetry.dominant_freq_hz.toFixed(1)} Hz` : '-'}</span>
          </div>
        </section>

        <section className="metrics-grid">
          <div className="metric-card cyan">
            <div className="metric-top">
              <span className="metric-icon"><Waves size={20} /></span>
              <span className="metric-live"><span /> 100 Hz FFT</span>
            </div>
            <p className="metric-label">DYNAMIC VIBRATION RMS</p>
            <div className="metric-value">
              {isFresh && Number.isFinite(currentTelemetry.vib_rms) ? currentTelemetry.vib_rms.toFixed(3) : '-'}
              {isFresh && Number.isFinite(currentTelemetry.vib_rms) && <span>m/s²</span>}
            </div>
            <p className="metric-description">Safety Limit: {vibLimitInput} m/s²</p>
          </div>

          <div className="metric-card purple">
            <div className="metric-top">
              <span className="metric-icon"><Thermometer size={20} /></span>
              <span className="metric-live"><span /> DS18B20</span>
            </div>
            <p className="metric-label">PROBE TEMPERATURE</p>
            <div className="metric-value">
              {isFresh && Number.isFinite(currentTelemetry.temp_c) ? currentTelemetry.temp_c.toFixed(1) : '-'}
              {isFresh && Number.isFinite(currentTelemetry.temp_c) && <span>°C</span>}
            </div>
            <p className="metric-description">Safety Limit: {tempLimitInput} °C</p>
          </div>

          <div className="metric-card blue">
            <div className="metric-top">
              <span className="metric-icon"><Zap size={20} /></span>
              <span className="metric-live"><span /> ACS712</span>
            </div>
            <p className="metric-label">CURRENT CONSUMPTION</p>
            <div className="metric-value">
              {!isFresh ? (
                'Offline'
              ) : currentTelemetry.current_status === 'disabled' || (currentTelemetry.current_a === null && !currentTelemetry.current_status) ? (
                'Disabled'
              ) : currentTelemetry.current_status === 'invalid' || currentTelemetry.current_status === 'disconnected' ? (
                <span style={{ color: '#ff7e85', fontSize: '0.85em' }}>Invalid</span>
              ) : Number.isFinite(currentTelemetry.current_a) && currentTelemetry.current_a !== null ? (
                <>
                  {currentTelemetry.current_a.toFixed(3)} <span>A</span>
                </>
              ) : (
                'Disabled'
              )}
            </div>
            <p className="metric-description">
              {currentTelemetry.current_status === 'invalid' || currentTelemetry.current_status === 'disconnected'
                ? 'Sensor error / disconnected'
                : `Safety Limit: ${currLimitInput} A`}
            </p>
          </div>

          <div className="metric-card green">
            <div className="metric-top">
              <span className="metric-icon"><Radio size={20} /></span>
              <span className="metric-live"><span /> WiFi RSSI</span>
            </div>
            <p className="metric-label">WIFI SIGNAL STRENGTH</p>
            <div className="metric-value">
              {isFresh && currentTelemetry.rssi !== null ? currentTelemetry.rssi : '-'}
              {isFresh && currentTelemetry.rssi !== null && <span>dBm</span>}
            </div>
            <p className="metric-description">
              {isFresh && currentTelemetry.uptime_s !== null ? `Uptime: ${Math.round(currentTelemetry.uptime_s / 60)} min` : 'Device offline'}
            </p>
          </div>
        </section>

        <section className="content-grid">
          <div className="panel chart-panel">
            <div className="panel-heading">
              <div>
                <h3>Real-Time Telemetry Trends</h3>
                <p>Adaptive baseline metrics and thermal trends</p>
              </div>
              <div className="live-tag"><span /> {isFresh ? 'STREAMING' : 'OFFLINE'}</div>
            </div>

            <div className="chart-wrap">
              {(!isFresh || telemetryHistory.length < 2) ? (
                <div className="chart-empty">
                  <Activity size={24} />
                  <span>{!isFresh ? 'Device offline: No real-time data' : 'Streaming sensor windows from device...'}</span>
                </div>
              ) : (
                <ResponsiveContainer width="100%" height={260}>
                  <LineChart data={telemetryHistory}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#1d2a3f" />
                    <XAxis dataKey="time" stroke="#5d708a" fontSize={10} tickLine={false} />
                    <YAxis yAxisId="vib" stroke="#38c8ee" fontSize={10} tickLine={false} domain={[0, 'auto']} />
                    <YAxis yAxisId="temp" orientation="right" stroke="#b69cff" fontSize={10} tickLine={false} domain={['auto', 'auto']} />
                    <Tooltip contentStyle={{ backgroundColor: '#0f172a', borderColor: '#334155', fontSize: '11px' }} />
                    <Line yAxisId="vib" type="monotone" dataKey="vib_rms" name="Vib RMS (m/s²)" stroke="#38c8ee" dot={false} strokeWidth={2} />
                    <Line yAxisId="temp" type="monotone" dataKey="temp_c" name="Temp (°C)" stroke="#b69cff" dot={false} strokeWidth={2} />
                  </LineChart>
                </ResponsiveContainer>
              )}
            </div>

            <div className="legend">
              <span><i className="legend-cyan" /> Vibration RMS (m/s²)</span>
              <span><i className="legend-purple" /> Temperature (°C)</span>
            </div>
          </div>

          <div className="panel control-panel">
            <div className="panel-heading">
              <div>
                <h3>Appliance Remote Controls</h3>
                <p>Validated and acknowledged via MQTT</p>
              </div>
              <Sliders size={18} className="muted-icon" />
            </div>

            <div className="calibration-top-block">
              <button
                className={`btn-calibrate-hero ${isLatched ? 'disabled' : ''}`}
                onClick={handleCalibrateBaseline}
                disabled={isLatched}
                title={isLatched ? 'Reset the latch first' : 'Calibrate Baseline'}
              >
                <RotateCcw size={15} className={isCalibrating ? 'spin-icon' : ''} />
                <span>{isCalibrating ? 'Calibrating Baseline...' : 'Calibrate Baseline'}</span>
              </button>
              {isLatched && (
                <div className="latch-notice-tag">
                  <ShieldAlert size={14} />
                  <span>Reset the latch first</span>
                </div>
              )}
            </div>

            <div className="control-section">
              <label className="control-label"><Power size={14} /> FAN OPERATING MODE</label>
              <div className="button-group">
                <button
                  className={`btn-mode ${currentTelemetry.mode === 'auto' ? 'active' : ''}`}
                  onClick={() => handleSetFanMode('auto')}
                  disabled={isLatched}
                >
                  AUTO
                </button>
                <button
                  className={`btn-mode ${currentTelemetry.mode === 'on' ? 'active' : ''}`}
                  onClick={() => handleSetFanMode('on')}
                  disabled={isLatched}
                >
                  FORCE ON
                </button>
                <button
                  className={`btn-mode ${currentTelemetry.mode === 'off' ? 'active' : ''}`}
                  onClick={() => handleSetFanMode('off')}
                  disabled={isLatched}
                >
                  FORCE OFF
                </button>
              </div>
              {isLatched && <small className="warn-text">Device is latched: Manual commands locked until reset</small>}
            </div>

            <div className="control-section">
              <div className="slider-header">
                <span>Max Temperature Limit</span>
                <strong>{tempLimitInput} °C</strong>
              </div>
              <input
                type="range" min="40" max="85" step="1"
                value={tempLimitInput}
                onChange={(e) => setTempLimitInput(e.target.value)}
                className="range-slider"
              />

              <div className="slider-header" style={{ marginTop: '12px' }}>
                <span>Max Vibration RMS Limit</span>
                <strong>{vibLimitInput} m/s²</strong>
              </div>
              <input
                type="range" min="1.0" max="10.0" step="0.2"
                value={vibLimitInput}
                onChange={(e) => setVibLimitInput(e.target.value)}
                className="range-slider"
              />

              <button
                className="btn-apply-limits"
                onClick={handleApplyLimits}
                disabled={isApplyingLimits}
              >
                {isApplyingLimits ? 'Applying Settings...' : 'Apply Hard Safety Limits'}
              </button>
            </div>

            <div className="quick-actions-row">
              <button className={`btn-utility ${buzzerMuted ? 'muted' : ''}`} onClick={handleToggleBuzzer}>
                {buzzerMuted ? <BellOff size={14} /> : <Bell size={14} />}
                {buzzerMuted ? 'Unmute Buzzer' : 'Mute Buzzer'}
              </button>
            </div>
          </div>
        </section>

        <section className="panel events-panel">
          <div className="panel-heading">
            <div>
              <h3>Appliance Event & Alert History</h3>
              <p>Edge anomaly events and safety latch state transitions</p>
            </div>
            <div className="event-count">{events.length} Alerts</div>
          </div>

          {events.length === 0 ? (
            <div className="empty-events">
              <CheckCircle2 size={24} />
              <div>
                <strong>Zero Anomalies Detected</strong>
                <span>Appliance is operating in nominal baseline parameters.</span>
              </div>
            </div>
          ) : (
            <div className="event-list">
              {events.map((evt) => (
                <div key={evt.id} className="event-row">
                  <div className={`event-icon ${evt.state.toLowerCase()}`}>
                    <AlertTriangle size={16} />
                  </div>
                  <div className="event-copy">
                    <strong>{evt.message}</strong>
                    <small>{evt.device_id} | {evt.time} | Reason: {evt.reason}</small>
                  </div>
                  <span className={`state-chip ${evt.state.toLowerCase()}`}>
                    {evt.state}
                  </span>
                </div>
              ))}
            </div>
          )}
        </section>
      </main>
    </div>
  );
}
