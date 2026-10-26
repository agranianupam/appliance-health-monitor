const initSqlJs = require("sql.js");
const fs = require("fs");
const path = require("path");

const DB_PATH = path.join(__dirname, "appliance.db");

let db;

async function initializeDatabase() {
  const SQL = await initSqlJs();

  if (fs.existsSync(DB_PATH)) {
    const fileBuffer = fs.readFileSync(DB_PATH);
    db = new SQL.Database(fileBuffer);
  } else {
    db = new SQL.Database();
  }

  db.run(`
    CREATE TABLE IF NOT EXISTS sensor_readings (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT NOT NULL,
      temperature_c REAL,
      current_rms_a REAL,
      vibration_rms_g REAL,
      health_state TEXT NOT NULL,
      confidence REAL,
      relay_state TEXT NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    )
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS fault_events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT NOT NULL,
      health_state TEXT NOT NULL,
      message TEXT NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    )
  `);

  saveDatabase();
  return db;
}

function saveDatabase() {
  if (!db) {
    throw new Error("Database has not been initialized");
  }

  const data = db.export();
  fs.writeFileSync(DB_PATH, Buffer.from(data));
}

function getDatabase() {
  if (!db) {
    throw new Error("Database has not been initialized");
  }

  return db;
}

module.exports = {
  initializeDatabase,
  getDatabase,
  saveDatabase
};
