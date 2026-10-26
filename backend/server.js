const express = require("express");
const cors = require("cors");

const {
  initializeDatabase,
  getDatabase,
  saveDatabase
} = require("./database");

const path = require("path");

const app = express();
const PORT = 5000;

app.use(cors());
app.use(express.json({ limit: "20kb" }));

const frontendDist = path.join(__dirname, "../frontend/dist");
app.use(express.static(frontendDist));

const allowedStates = [
  "Normal",
  "Early Abnormality",
  "Maintenance Recommended",
  "Critical",
  "NORMAL",
  "WARNING",
  "CRITICAL",
  "CALIBRATING",
  "SENSOR_ERROR"
];

function ensureAccelerationColumns() {
  const db = getDatabase();

  const result = db.exec("PRAGMA table_info(sensor_readings)");

  if (!result.length) {
    throw new Error("sensor_readings table was not found");
  }

  const columns = result[0].values.map(row => row[1]);

  const additions = [
    ["accel_x", "REAL"],
    ["accel_y", "REAL"],
    ["accel_z", "REAL"]
  ];

  for (const [name, type] of additions) {
    if (!columns.includes(name)) {
      db.run(
        `ALTER TABLE sensor_readings ADD COLUMN ${name} ${type}`
      );
    }
  }

  saveDatabase();
}

app.get("/api/health", (req, res) => {
  res.json({
    status: "ok",
    service: "appliance-health-api"
  });
});

app.post("/api/readings", (req, res) => {
  try {
    const r = req.body;

    const numericFields = [
      r?.temperature_c,
      r?.current_rms_a,
      r?.vibration_rms_g,
      r?.accel_x,
      r?.accel_y,
      r?.accel_z
    ];

    if (
      !r ||
      typeof r.device_id !== "string" ||
      !r.device_id.trim() ||
      r.device_id.length > 80 ||
      !numericFields.every(Number.isFinite) ||
      !allowedStates.includes(r.health_state) ||
      !Number.isFinite(r.confidence) ||
      r.confidence < 0 ||
      r.confidence > 1 ||
      !["ON", "OFF", "UNKNOWN"].includes(r.relay_state)
    ) {
      return res.status(400).json({
        error: "Invalid sensor packet"
      });
    }

    const db = getDatabase();

    const previousResult = db.exec(
      `SELECT health_state
       FROM sensor_readings
       WHERE device_id = ?
       ORDER BY id DESC LIMIT 1`,
      [r.device_id]
    );

    let previousState = null;

    if (
      previousResult.length &&
      previousResult[0].values.length
    ) {
      previousState = previousResult[0].values[0][0];
    }

    db.run(
      `INSERT INTO sensor_readings (
        device_id,
        temperature_c,
        current_rms_a,
        vibration_rms_g,
        health_state,
        confidence,
        relay_state,
        accel_x,
        accel_y,
        accel_z
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      [
        r.device_id,
        r.temperature_c,
        r.current_rms_a,
        r.vibration_rms_g,
        r.health_state,
        r.confidence,
        r.relay_state,
        r.accel_x,
        r.accel_y,
        r.accel_z
      ]
    );

    const idResult = db.exec(
      "SELECT last_insert_rowid()"
    );

    const id = idResult[0].values[0][0];

    if (
      r.health_state !== "Normal" &&
      r.health_state !== previousState
    ) {
      db.run(
        `INSERT INTO fault_events (
          device_id, health_state, message
        ) VALUES (?, ?, ?)`,
        [
          r.device_id,
          r.health_state,
          `Health state changed to ${r.health_state}`
        ]
      );
    }

    saveDatabase();

    res.status(201).json({
      success: true,
      id
    });
  } catch (error) {
    console.error("Reading error:", error);

    res.status(500).json({
      error: "Could not save sensor reading"
    });
  }
});

function rowsToObjects(result) {
  if (!result.length) return [];

  const columns = result[0].columns;

  return result[0].values.map(values => {
    const row = {};

    columns.forEach((column, index) => {
      row[column] = values[index];
    });

    return row;
  });
}

app.get("/api/latest", (req, res) => {
  const db = getDatabase();

  const result = db.exec(
    "SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 1"
  );

  const rows = rowsToObjects(result);

  res.json(rows[0] || null);
});

app.get("/api/history", (req, res) => {
  const parsedLimit = Number.parseInt(req.query.limit, 10);

  const limit = Number.isFinite(parsedLimit)
    ? Math.min(Math.max(parsedLimit, 1), 500)
    : 100;

  const db = getDatabase();

  const result = db.exec(
    `SELECT * FROM (
       SELECT * FROM sensor_readings
       ORDER BY id DESC LIMIT ${limit}
     ) ORDER BY id ASC`
  );

  res.json(rowsToObjects(result));
});

app.get("/api/events", (req, res) => {
  const db = getDatabase();

  const result = db.exec(
    "SELECT * FROM fault_events ORDER BY id DESC LIMIT 100"
  );

  res.json(rowsToObjects(result));
});

app.use((req, res, next) => {
  if (req.path.startsWith("/api/")) {
    return next();
  }
  res.sendFile(path.join(frontendDist, "index.html"));
});

async function startServer() {
  try {
    await initializeDatabase();
    ensureAccelerationColumns();

    app.listen(PORT, "0.0.0.0", () => {
      console.log(`API running on port ${PORT}`);
      console.log(`Database: ${__dirname}/appliance.db`);
    });
  } catch (error) {
    console.error("Failed to start backend:", error);
    process.exit(1);
  }
}

startServer();
