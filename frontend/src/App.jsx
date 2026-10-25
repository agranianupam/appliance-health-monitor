
import { useEffect, useMemo, useState } from 'react';
import {
  Activity,
  AlertTriangle,
  CheckCircle2,
  Clock3,
  Cpu,
  Gauge,
  Radio,
  RefreshCw,
  ShieldCheck,
  Thermometer,
  Waves,
  Zap,
} from 'lucide-react';

import {
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';

import { getEvents, getHistory, getLatest } from './services/api';

const stateClass = (state = 'Awaiting data') => {
  if (state === 'Normal') return 'good';
  if (state === 'Early Abnormality') return 'watch';
  if (state === 'Maintenance Recommended') return 'warn';
  if (state === 'Critical' || state === 'Sensor Fault') return 'critical';
  return 'neutral';
};

const fmt = (value, digits = 2) =>
  Number.isFinite(Number(value))
    ? Number(value).toFixed(digits)
    : '—';

function MetricCard({
  icon: Icon,
  title,
  value,
  unit,
  description,
  tone,
}) {
  return (
    <article className={`metric-card ${tone}`}>
      <div className="metric-top">
        <span className="metric-icon">
          <Icon size={19} />
        </span>
        <span className="metric-live">
          <span /> LIVE FEED
        </span>
      </div>

      <p className="metric-label">{title}</p>

      <div className="metric-value">
        {value}
        <span>{unit}</span>
      </div>

      <p className="metric-description">{description}</p>
    </article>
  );
}

function TrendChart({ history }) {
  const data = useMemo(
    () =>
      history.map((row) => ({
        ...row,
        time: row.created_at
          ? new Date(row.created_at).toLocaleTimeString([], {
              hour: '2-digit',
              minute: '2-digit',
              second: '2-digit',
            })
          : `#${row.id}`,
      })),
    [history]
  );

  return (
    <div className="chart-wrap">
      {data.length < 2 ? (
        <div className="chart-empty">
          <Activity size={24} />
          <span>
            Waiting for more readings to draw a trend
          </span>
        </div>
      ) : (
        <ResponsiveContainer width="100%" height="100%">
          <LineChart
            data={data}
            margin={{
              top: 12,
              right: 12,
              left: -16,
              bottom: 4,
            }}
          >
            <CartesianGrid
              stroke="#233047"
              strokeDasharray="4 5"
            />

            <XAxis
              dataKey="time"
              tick={{ fill: '#8190a8', fontSize: 10 }}
              tickLine={false}
              axisLine={false}
              minTickGap={28}
            />

            <YAxis
              yAxisId="temp"
              tick={{ fill: '#8190a8', fontSize: 10 }}
              tickLine={false}
              axisLine={false}
            />

            <YAxis
              yAxisId="current"
              orientation="right"
              tick={{ fill: '#8190a8', fontSize: 10 }}
              tickLine={false}
              axisLine={false}
            />

            <Tooltip
              contentStyle={{
                background: '#101a2b',
                border: '1px solid #2a3850',
                borderRadius: 10,
                color: '#eaf0fb',
              }}
            />

            <Line
              yAxisId="temp"
              type="monotone"
              dataKey="temperature_c"
              name="Temperature °C"
              stroke="#36c5f0"
              strokeWidth={2.5}
              dot={false}
              isAnimationActive={false}
            />

            <Line
              yAxisId="current"
              type="monotone"
              dataKey="current_rms_a"
              name="Current A"
              stroke="#b69cff"
              strokeWidth={2.2}
              dot={false}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      )}
    </div>
  );
}

export default function App() {
  const [reading, setReading] = useState(null);
  const [history, setHistory] = useState([]);
  const [events, setEvents] = useState([]);
  const [connected, setConnected] = useState(false);
  const [lastChecked, setLastChecked] = useState(null);
  const [error, setError] = useState('');

  // Current selected page
  const [activePage, setActivePage] = useState('overview');

  // Load readings and refresh every two seconds
  useEffect(() => {
    let active = true;

    async function refresh() {
      try {
        const [latest, rows, eventRows] = await Promise.all([
          getLatest(),
          getHistory(),
          getEvents(),
        ]);

        if (!active) return;

        setReading(latest);
        setHistory(rows);
        setEvents(eventRows);
        setConnected(true);
        setError('');
        setLastChecked(new Date());
      } catch (e) {
        if (!active) return;

        setConnected(false);
        setError(e.message || 'Could not connect to backend');
        setLastChecked(new Date());
      }
    }

    refresh();

    const timer = setInterval(refresh, 2000);

    return () => {
      active = false;
      clearInterval(timer);
    };
  }, []);

  const ageMs = reading?.created_at
    ? Date.now() - new Date(reading.created_at).getTime()
    : Infinity;

  const isFresh = Number.isFinite(ageMs) && ageMs < 15000;
  const status = reading ? reading.health_state : 'Awaiting data';
  const health = stateClass(status);

  const pageLabels = {
    overview: 'OVERVIEW',
    analytics: 'SENSOR ANALYTICS',
    diagnosis: 'AI DIAGNOSIS',
    history: 'FAULT HISTORY',
  };

  const pageTitles = {
    overview: 'Appliance health monitor',
    analytics: 'Sensor Analytics',
    diagnosis: 'AI Diagnosis',
    history: 'Fault History',
  };

  return (
    <div className="app-shell">
      {/* SIDEBAR */}
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark">
            <Activity size={22} />
          </div>

          <div>
            <strong>APEX</strong>
            <span>APPLIANCE INTELLIGENCE</span>
          </div>
        </div>

        <div className="side-section">WORKSPACE</div>

        <div
          className={`nav-item ${
            activePage === 'overview' ? 'active' : ''
          }`}
          onClick={() => setActivePage('overview')}
          role="button"
          tabIndex={0}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              setActivePage('overview');
            }
          }}
          style={{ cursor: 'pointer' }}
        >
          <Gauge size={17} />
          Overview
          {activePage === 'overview' && <span className="nav-dot" />}
        </div>

        <div
          className={`nav-item ${
            activePage === 'analytics' ? 'active' : ''
          }`}
          onClick={() => setActivePage('analytics')}
          role="button"
          tabIndex={0}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              setActivePage('analytics');
            }
          }}
          style={{ cursor: 'pointer' }}
        >
          <Waves size={17} />
          Sensor analytics
          {activePage === 'analytics' && <span className="nav-dot" />}
        </div>

        <div
          className={`nav-item ${
            activePage === 'diagnosis' ? 'active' : ''
          }`}
          onClick={() => setActivePage('diagnosis')}
          role="button"
          tabIndex={0}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              setActivePage('diagnosis');
            }
          }}
          style={{ cursor: 'pointer' }}
        >
          <Cpu size={17} />
          AI diagnosis
          {activePage === 'diagnosis' && <span className="nav-dot" />}
        </div>

        <div
          className={`nav-item ${
            activePage === 'history' ? 'active' : ''
          }`}
          onClick={() => setActivePage('history')}
          role="button"
          tabIndex={0}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              setActivePage('history');
            }
          }}
          style={{ cursor: 'pointer' }}
        >
          <AlertTriangle size={17} />
          Fault history
          <span className="nav-count">{events.length}</span>
        </div>

        <div className="sidebar-bottom">
          <div className="device-mini">
            <span
              className={`connection-dot ${
                connected && isFresh ? 'online' : ''
              }`}
            />

            <div>
              <strong>ESP32-FAN-01</strong>
              <small>
                {connected && isFresh
                  ? 'Device reporting'
                  : 'Waiting for device'}
              </small>
            </div>
          </div>

          <p>LOCAL MONITORING CONSOLE</p>
        </div>
      </aside>

      {/* MAIN CONTENT */}
      <main className="main-content">
        <header className="topbar">
          <div>
            <div className="eyebrow">
              PREDICTIVE MAINTENANCE / {pageLabels[activePage]}
            </div>

            <h1>{pageTitles[activePage]}</h1>

            <p className="subtitle">
              Real-time condition intelligence for your connected appliance.
            </p>
          </div>

          <div className="topbar-right">
            <span
              className={`connection-pill ${
                connected && isFresh ? 'connected' : ''
              }`}
            >
              <span />
              {connected && isFresh
                ? 'DEVICE ONLINE'
                : connected
                  ? 'DATA STALE'
                  : 'API OFFLINE'}
            </span>

            <div className="updated">
              <RefreshCw size={13} />
              {lastChecked
                ? `Checked ${lastChecked.toLocaleTimeString()}`
                : 'Connecting…'}
            </div>
          </div>
        </header>

        {/* OVERVIEW PAGE */}
        {activePage === 'overview' && (
          <>
            {error && (
              <div className="error-banner">
                <AlertTriangle size={17} />
                <div>
                  <strong>Backend connection issue</strong>
                  <span>
                    {error}. Start the backend server and refresh this page.
                  </span>
                </div>
              </div>
            )}

            {!reading && !error && (
              <div className="info-banner">
                <Radio size={17} />
                Waiting for the first sensor packet. No real readings have
                arrived yet.
              </div>
            )}

            <section className="section-heading">
              <div>
                <h2>Live sensor telemetry</h2>
                <p>Measurements received from your ESP32</p>
              </div>

              <span className="refresh-label">
                <span className="pulse-dot" />
                AUTO-REFRESH · 2 SEC
              </span>
            </section>

            <section className="metrics-grid">
              <MetricCard
                icon={Thermometer}
                title="Motor temperature"
                value={reading ? fmt(reading.temperature_c, 1) : '—'}
                unit="°C"
                description="DS18B20 temperature probe"
                tone="cyan"
              />

              <MetricCard
                icon={Zap}
                title="Current RMS"
                value={reading ? fmt(reading.current_rms_a, 3) : '—'}
                unit="A"
                description="ACS712 + ADS1115"
                tone="purple"
              />

              <MetricCard
                icon={Activity}
                title="Vibration RMS"
                value={reading ? fmt(reading.vibration_rms_g, 3) : '—'}
                unit="g"
                description="MPU6050 acceleration feature"
                tone="blue"
              />

              <article className={`health-card ${health}`}>
                <div className="metric-top">
                  <span className="metric-icon">
                    <ShieldCheck size={19} />
                  </span>
                  <span className="health-label">HEALTH CLASS</span>
                </div>

                <p className="metric-label">AI health assessment</p>

                <div className="health-state">{status}</div>

                <p className="metric-description">
                  {reading?.confidence != null
                    ? `Model confidence: ${fmt(
                        reading.confidence * 100,
                        1
                      )}%`
                    : 'Confidence not supplied by device'}
                </p>

                <div className="health-footer">
                  <span className="health-indicator" />
                  {reading
                    ? `Relay ${reading.relay_state}`
                    : 'Awaiting device data'}
                </div>
              </article>
            </section>

            <section className="content-grid">
              <article className="panel chart-panel">
                <div className="panel-heading">
                  <div>
                    <h3>Sensor trends</h3>
                    <p>
                      Latest {history.length} stored readings · temperature
                      and current
                    </p>
                  </div>

                  <span className="live-tag">
                    <span /> LIVE
                  </span>
                </div>

                <div className="legend">
                  <span>
                    <i className="legend-cyan" /> Temperature (°C)
                  </span>
                  <span>
                    <i className="legend-purple" /> Current (A)
                  </span>
                </div>

                <TrendChart history={history} />
              </article>

              <article className="panel device-panel">
                <div className="panel-heading">
                  <div>
                    <h3>Device status</h3>
                    <p>Connection and protection overview</p>
                  </div>
                  <Cpu size={18} className="muted-icon" />
                </div>

                <div className="device-row">
                  <span className="device-icon">
                    <Radio size={18} />
                  </span>

                  <div>
                    <strong>ESP32-FAN-01</strong>
                    <small>Edge controller</small>
                  </div>

                  <span
                    className={`status-text ${
                      connected && isFresh ? 'ok' : 'bad'
                    }`}
                  >
                    {connected && isFresh ? 'Online' : 'Offline'}
                  </span>
                </div>

                <div className="device-stat">
                  <span>Backend API</span>
                  <strong className={connected ? 'ok' : 'bad'}>
                    {connected ? 'Reachable' : 'Unavailable'}
                  </strong>
                </div>

                <div className="device-stat">
                  <span>Latest packet</span>
                  <strong>
                    {reading?.created_at
                      ? new Date(reading.created_at).toLocaleTimeString()
                      : 'No data'}
                  </strong>
                </div>

                <div className="device-stat">
                  <span>Relay state</span>
                  <strong>{reading?.relay_state || 'Unknown'}</strong>
                </div>

                <div className="safety-note">
                  <ShieldCheck size={17} />
                  <p>
                    <strong>Local protection</strong>
                    <br />
                    Critical shutdown must remain controlled by the ESP32,
                    not the web dashboard.
                  </p>
                </div>
              </article>
            </section>

            <section className="panel events-panel">
              <div className="panel-heading">
                <div>
                  <h3>Recent fault events</h3>
                  <p>
                    Health-state transitions recorded by the backend
                  </p>
                </div>

                <span className="event-count">
                  {events.length} EVENTS
                </span>
              </div>

              {events.length === 0 ? (
                <div className="empty-events">
                  <CheckCircle2 size={20} />
                  <div>
                    <strong>No fault events recorded</strong>
                    <span>
                      Events will appear here when the backend receives a
                      non-normal health state.
                    </span>
                  </div>
                </div>
              ) : (
                <div className="event-list">
                  {events.slice(0, 6).map((event) => (
                    <div className="event-row" key={event.id}>
                      <span
                        className={`event-icon ${stateClass(
                          event.health_state
                        )}`}
                      >
                        <AlertTriangle size={16} />
                      </span>

                      <div className="event-copy">
                        <strong>{event.message}</strong>
                        <small>
                          {event.device_id} ·{' '}
                          {new Date(event.created_at).toLocaleString()}
                        </small>
                      </div>

                      <span
                        className={`state-chip ${stateClass(
                          event.health_state
                        )}`}
                      >
                        {event.health_state}
                      </span>
                    </div>
                  ))}
                </div>
              )}
            </section>
          </>
        )}

        {/* SENSOR ANALYTICS PAGE */}
        {activePage === 'analytics' && (
          <>
            <section className="section-heading">
              <div>
                <h2>Sensor Analytics</h2>
                <p>
                  Measurements and trends from the connected appliance
                </p>
              </div>

              <span className="refresh-label">
                <span className="pulse-dot" />
                AUTO-REFRESH · 2 SEC
              </span>
            </section>

            <section className="metrics-grid">
              <MetricCard
                icon={Thermometer}
                title="Temperature"
                value={reading ? fmt(reading.temperature_c, 1) : '—'}
                unit="°C"
                description="Latest DS18B20 reading"
                tone="cyan"
              />

              <MetricCard
                icon={Zap}
                title="Current RMS"
                value={reading ? fmt(reading.current_rms_a, 3) : '—'}
                unit="A"
                description="Latest current measurement"
                tone="purple"
              />

              <MetricCard
                icon={Activity}
                title="Vibration RMS"
                value={reading ? fmt(reading.vibration_rms_g, 3) : '—'}
                unit="g"
                description="Latest vibration measurement"
                tone="blue"
              />
            </section>

            <section className="panel chart-panel">
              <div className="panel-heading">
                <div>
                  <h3>Temperature and current trends</h3>
                  <p>{history.length} stored readings</p>
                </div>

                <span className="live-tag">
                  <span /> LIVE
                </span>
              </div>

              <div className="legend">
                <span>
                  <i className="legend-cyan" /> Temperature (°C)
                </span>
                <span>
                  <i className="legend-purple" /> Current (A)
                </span>
              </div>

              <TrendChart history={history} />
            </section>

            <section className="panel events-panel">
              <div className="panel-heading">
                <div>
                  <h3>Reading summary</h3>
                  <p>Latest values received from the backend</p>
                </div>
              </div>

              <div className="device-stat">
                <span>Total stored readings</span>
                <strong>{history.length}</strong>
              </div>

              <div className="device-stat">
                <span>Latest temperature</span>
                <strong>
                  {reading
                    ? `${fmt(reading.temperature_c, 1)} °C`
                    : 'No data'}
                </strong>
              </div>

              <div className="device-stat">
                <span>Latest current</span>
                <strong>
                  {reading
                    ? `${fmt(reading.current_rms_a, 3)} A`
                    : 'No data'}
                </strong>
              </div>

              <div className="device-stat">
                <span>Latest vibration</span>
                <strong>
                  {reading
                    ? `${fmt(reading.vibration_rms_g, 3)} g`
                    : 'No data'}
                </strong>
              </div>
            </section>
          </>
        )}

        {/* AI DIAGNOSIS PAGE */}
        {activePage === 'diagnosis' && (
          <>
            <section className="section-heading">
              <div>
                <h2>AI Diagnosis</h2>
                <p>
                  Review the health classification reported by your device
                </p>
              </div>
            </section>

            {!reading ? (
              <section className="panel events-panel">
                <div className="empty-events">
                  <Radio size={20} />
                  <div>
                    <strong>No sensor readings available</strong>
                    <span>
                      Connect the ESP32 and send a reading to view its
                      reported health state.
                    </span>
                  </div>
                </div>
              </section>
            ) : (
              <>
                <section className="metrics-grid">
                  <article className={`health-card ${health}`}>
                    <div className="metric-top">
                      <span className="metric-icon">
                        <ShieldCheck size={19} />
                      </span>
                      <span className="health-label">HEALTH CLASS</span>
                    </div>

                    <p className="metric-label">Reported health status</p>

                    <div className="health-state">{status}</div>

                    <p className="metric-description">
                      {reading.confidence != null
                        ? `Reported model confidence: ${fmt(
                            reading.confidence * 100,
                            1
                          )}%`
                        : 'Model confidence not provided'}
                    </p>
                  </article>

                  <MetricCard
                    icon={Thermometer}
                    title="Temperature"
                    value={fmt(reading.temperature_c, 1)}
                    unit="°C"
                    description="Latest temperature"
                    tone="cyan"
                  />

                  <MetricCard
                    icon={Zap}
                    title="Current RMS"
                    value={fmt(reading.current_rms_a, 3)}
                    unit="A"
                    description="Latest current"
                    tone="purple"
                  />
                </section>

                <section className="panel events-panel">
                  <div className="panel-heading">
                    <div>
                      <h3>Diagnostic details</h3>
                      <p>Values supplied by the latest sensor packet</p>
                    </div>
                    <Cpu size={20} />
                  </div>

                  <div className="device-stat">
                    <span>Health state</span>
                    <strong>{reading.health_state}</strong>
                  </div>

                  <div className="device-stat">
                    <span>Vibration RMS</span>
                    <strong>
                      {fmt(reading.vibration_rms_g, 3)} g
                    </strong>
                  </div>

                  <div className="device-stat">
                    <span>Relay state</span>
                    <strong>{reading.relay_state}</strong>
                  </div>

                  <div className="device-stat">
                    <span>Reading timestamp</span>
                    <strong>
                      {reading.created_at
                        ? new Date(reading.created_at).toLocaleString()
                        : 'Not available'}
                    </strong>
                  </div>

                  <div className="safety-note">
                    <ShieldCheck size={17} />
                    <p>
                      <strong>Diagnosis information</strong>
                      <br />
                      This page displays the health state supplied by the
                      device. A trained TinyML model must be implemented
                      and tested before this can be treated as a genuine
                      AI diagnosis.
                    </p>
                  </div>
                </section>
              </>
            )}
          </>
        )}

        {/* FAULT HISTORY PAGE */}
        {activePage === 'history' && (
          <>
            <section className="section-heading">
              <div>
                <h2>Fault History</h2>
                <p>
                  Health-state changes recorded by the backend
                </p>
              </div>

              <span className="event-count">
                {events.length} EVENTS
              </span>
            </section>

            <section className="panel events-panel">
              <div className="panel-heading">
                <div>
                  <h3>Recorded fault events</h3>
                  <p>Newest events appear first</p>
                </div>

                <AlertTriangle size={20} />
              </div>

              {events.length === 0 ? (
                <div className="empty-events">
                  <CheckCircle2 size={20} />
                  <div>
                    <strong>No fault events recorded</strong>
                    <span>
                      This list will populate when the backend receives
                      a non-normal health state that creates a fault event.
                    </span>
                  </div>
                </div>
              ) : (
                <div className="event-list">
                  {events.map((event) => (
                    <div className="event-row" key={event.id}>
                      <span
                        className={`event-icon ${stateClass(
                          event.health_state
                        )}`}
                      >
                        <AlertTriangle size={16} />
                      </span>

                      <div className="event-copy">
                        <strong>{event.message}</strong>
                        <small>
                          Device: {event.device_id}
                          {' · '}
                          {new Date(event.created_at).toLocaleString()}
                        </small>
                      </div>

                      <span
                        className={`state-chip ${stateClass(
                          event.health_state
                        )}`}
                      >
                        {event.health_state}
                      </span>
                    </div>
                  ))}
                </div>
              )}
            </section>
          </>
        )}

        {/* FOOTER */}
        <footer>
          <span>
            APEX <b>·</b> IoT Appliance Health Monitor
          </span>

          <span>
            <Clock3 size={13} />
            {reading?.created_at
              ? `Last reading ${new Date(
                  reading.created_at
                ).toLocaleString()}`
              : 'No sensor reading received'}
          </span>
        </footer>
      </main>
    </div>
  );
}
