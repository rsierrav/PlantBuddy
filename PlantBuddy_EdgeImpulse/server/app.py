from flask import Flask, request, Response
import sqlite3, json, queue, threading, os
from flask_cors import CORS

# -----------------------------
# Database file (same folder)
# -----------------------------
DB = "plantbuddy.db"

app = Flask(__name__)
CORS(app)
# -----------------------------
# Ensure DB schema exists
# -----------------------------
def ensure_schema():
    con = sqlite3.connect(DB)
    cur = con.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS plant_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            soil_moisture REAL,
            light_level REAL,
            temperature REAL,
            humidity REAL,
            pump_state INTEGER,
            condition TEXT
        )
    """)
    con.commit()
    con.close()

# -----------------------------
# Insert a new row
# -----------------------------
def insert_row(d):
    con = sqlite3.connect(DB)
    cur = con.cursor()
    try:
        cur.execute("""
            INSERT INTO plant_data
            (plant_id, soil_moisture, light_level, temperature, humidity, pump_state, condition)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (d["plant_id"], d["soil"], d["light"], d["temp"], d["humidity"], d["pump"], d["condition"]))
        con.commit()
        print("🌱 INSERT OK", d)
    except Exception as e:
        print("❌ INSERT FAILED:", e)
    con.close()

    # notify SSE subscribers
    try:
        payload = {
            "soil": d["soil"], "light": d["light"], "temp": d["temp"],
            "humidity": d["humidity"], "pump": d.get("pump", 0),
            "condition": d.get("condition", "")
        }
        for q in list(_sse_clients):
            try:
                q.put(payload, block=False)
            except:
                pass
    except:
        pass

# SSE storage
_sse_clients, _sse_lock = [], threading.Lock()

# -----------------------------
# Basic health route
# -----------------------------
@app.get("/")
def root():
    return {"ok": True, "service": "plant-buddy-data-server"}

@app.get("/health")
def health():
    return {"status": "healthy"}

# -----------------------------
# Ingest JSON from ESP32
# -----------------------------
@app.post("/ingest")
def ingest():
    raw = request.get_json(silent=True)
    if not raw:
        return {"error": "invalid or missing JSON body"}, 400

    print("📡 Incoming data:", raw)

    data = {
        "plant_id": raw.get("plant_id", "unknown"),
        "soil": raw.get("soil") or raw.get("soil_moisture_pct"),
        "light": raw.get("light") or raw.get("light_lux"),
        "temp": raw.get("temp") or raw.get("temperature"),
        "humidity": raw.get("humidity") or raw.get("hum"),
        "pump": raw.get("pump") or raw.get("pump_state") or 0,
        "condition": raw.get("condition", "ok"),
    }

    if any(v is None for v in (data["soil"], data["light"], data["temp"], data["humidity"])):
        return {"error": "missing required fields"}, 400

    insert_row(data)
    return {"ok": True}, 200

# -----------------------------
# LIVE stream (SSE)
# -----------------------------
@app.get("/stream")
def stream():
    def gen(q):
        try:
            while True:
                data = q.get()
                yield 'data: ' + json.dumps(data) + '\n\n'
        except GeneratorExit:
            return

    q = queue.Queue()
    with _sse_lock:
        _sse_clients.append(q)

    return Response(gen(q), mimetype='text/event-stream')

# --------------------------------------------------------
# ⭐ FIXED LOCATION — REGISTERED BEFORE app.run()
# --------------------------------------------------------
@app.get("/latest")
def latest():
    plant = request.args.get("plant")     # <-- get plant name from URL

    con = sqlite3.connect(DB)
    cur = con.cursor()

    if plant:
        # return latest reading for a specific plant
        cur.execute("""
            SELECT timestamp, soil_moisture, light_level, temperature, humidity, pump_state, condition
            FROM plant_data
            WHERE plant_id = ?
            ORDER BY id DESC LIMIT 1
        """, (plant,))
    else:
        # default behavior (latest overall)
        cur.execute("""
            SELECT timestamp, soil_moisture, light_level, temperature, humidity, pump_state, condition
            FROM plant_data
            ORDER BY id DESC LIMIT 1
        """)

    row = cur.fetchone()
    con.close()

    if not row:
        return {"error": "no data yet"}, 404

    return {
        "timestamp": row[0],
        "soil": row[1],
        "light": row[2],
        "temp": row[3],
        "humidity": row[4],
        "pump": row[5],
        "condition": row[6],
    }


# -----------------------------
# Run server
# -----------------------------
if __name__ == "__main__":
    ensure_schema()
    print("🚀 Starting PlantBuddy backend on http://127.0.0.1:5000")
    app.run(host="0.0.0.0", port=5000)
