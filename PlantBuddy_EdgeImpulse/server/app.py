from flask import Flask, request, Response
import sqlite3
import csv
import io
import os
import json
import queue
import threading

DB = os.getenv(
    "DATABASE_URL_SQLITE",
    "plantbuddy.db",
)  # simple override via env if you want
app = Flask(__name__)


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


def insert_row(d):
    con = sqlite3.connect(DB)
    cur = con.cursor()
    cur.execute(
        """
        INSERT INTO plant_data
        (
            soil_moisture,
            light_level,
            temperature,
            humidity,
            pump_state,
            condition
        )
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (
            d["soil"],
            d["light"],
            d["temp"],
            d["humidity"],
            d["pump"],
            d["condition"],
        ),
    )
    con.commit()
    con.close()
    # Notify any live-stream clients about the new row
    try:
        payload = {
            "soil": d["soil"],
            "light": d["light"],
            "temp": d["temp"],
            "humidity": d["humidity"],
            "pump": d.get("pump", 0),
            "condition": d.get("condition", ""),
        }
        for q in list(_sse_clients):
            try:
                q.put(payload, block=False)
            except Exception:
                # ignore client queue errors
                pass
    except Exception:
        pass


# Ensure DB schema exists even when launched via `flask run`
ensure_schema()

# Simple SSE (server-sent events) support: hold small queues per connected client
_sse_clients = []  # list of queue.Queue
_sse_lock = threading.Lock()


@app.get("/")
def root():
    return {"ok": True, "service": "plant-buddy-data-server"}


@app.get("/health")
def health():
    return {"status": "healthy"}


@app.post("/ingest")
def ingest():
    # Parse JSON safely
    raw = request.get_json(silent=True)
    if not raw:
        return {"error": "invalid or missing JSON body"}, 400

    # Normalize incoming keys (accept short or verbose names)
    data = {
        "soil": (raw.get("soil") or raw.get("soil_moisture_pct")
                 or raw.get("soil_moisture_raw")),
        "light": (raw.get("light") or raw.get("light_lux") or raw.get("ldr")),
        "temp": (raw.get("temp") or raw.get("temperature")
                 or raw.get("temperature_c")),
        "humidity": (raw.get("humidity") or raw.get("hum")
                     or raw.get("humidity_pct")),
        "pump":
        raw.get("pump", 0),
        "condition":
        raw.get("condition", "ok"),
    }

    # Validate required fields
    missing = [
        k for k in ("soil", "light", "temp", "humidity") if data.get(k) is None
    ]
    if missing:
        return {"error": f"missing fields: {', '.join(missing)}"}, 400

    insert_row(data)
    return {"ok": True}, 200


@app.get('/stream')
def stream():

    def gen(q):
        try:
            while True:
                data = q.get()
                ev = 'data: ' + json.dumps(data) + '\n\n'
                yield ev
        except GeneratorExit:
            return

    q = queue.Queue()
    with _sse_lock:
        _sse_clients.append(q)

    return Response(gen(q), mimetype='text/event-stream')


@app.get('/live')
def live_ui():
    # Serve the simple HTML UI for live streaming and labeling
    return app.send_static_file('index.html')


@app.post("/label")
def insert_labeled():
    """Insert a labeled training sample.
    Accepts JSON with soil, light, temp, humidity, label.
    """
    raw = request.get_json(silent=True)
    if not raw:
        return {"error": "invalid or missing JSON body"}, 400

    data = {
        "soil": raw.get("soil"),
        "light": raw.get("light"),
        "temp": raw.get("temp"),
        "humidity": raw.get("humidity"),
        "pump": raw.get("pump", 0),
        "condition": raw.get("label") or raw.get("condition") or "",
    }

    missing = [
        k for k in ("soil", "light", "temp", "humidity") if data.get(k) is None
    ]
    if missing:
        return {"error": f"missing fields: {', '.join(missing)}"}, 400

    insert_row(data)
    return {"ok": True}, 200


@app.get("/export-ei-csv")
def export_ei_csv():
    """
    Export CSV formatted for Edge Impulse ingestion:
    columns soil, light, temp, humidity, label
    """
    con = sqlite3.connect(DB)
    cur = con.cursor()
    cur.execute("""
        SELECT
            soil_moisture,
            light_level,
            temperature,
            humidity,
            condition
        FROM plant_data
        ORDER BY timestamp
        """)
    rows = cur.fetchall()
    con.close()

    out = io.StringIO()
    w = csv.writer(out)
    w.writerow(["soil", "light", "temp", "humidity", "label"])
    # Convert None/NULL labels to empty string
    for r in rows:
        w.writerow([r[0], r[1], r[2], r[3], r[4] or ""])

    return Response(
        out.getvalue(),
        mimetype="text/csv",
        headers={
            "Content-Disposition":
            ("attachment; filename=plant_data_for_ei.csv")
        },
    )


@app.get("/export-csv")
def export_csv():
    con = sqlite3.connect(DB)
    cur = con.cursor()
    cur.execute("""
        SELECT
            timestamp,
            soil_moisture,
            light_level,
            temperature,
            humidity,
            pump_state,
            condition
        FROM plant_data
        ORDER BY timestamp
        """)
    rows = cur.fetchall()
    con.close()

    out = io.StringIO()
    w = csv.writer(out)
    w.writerow([
        "timestamp",
        "soil_moisture",
        "light_level",
        "temperature",
        "humidity",
        "pump_state",
        "condition",
    ])
    w.writerows(rows)

    return Response(
        out.getvalue(),
        mimetype="text/csv",
        headers={
            "Content-Disposition":
            ("attachment; filename=plant_data_for_ei.csv")
        },
    )


if __name__ == "__main__":
    # Running via `python app.py`
    ensure_schema()
    app.run(host="0.0.0.0", port=5000)
