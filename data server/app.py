from flask import Flask, request, Response
import sqlite3
import csv
import io
import os

DB = os.getenv(
    "DATABASE_URL_SQLITE",
    "plantbuddy.db",
)  # simple override via env if you want
app = Flask(__name__)


def ensure_schema():
    con = sqlite3.connect(DB)
    cur = con.cursor()
    cur.execute(
        """
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
        """
    )
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


# Ensure DB schema exists even when launched via `flask run`
ensure_schema()


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
        "soil": (
            raw.get("soil")
            or raw.get("soil_moisture_pct")
            or raw.get("soil_moisture_raw")
        ),
        "light": (
            raw.get("light")
            or raw.get("light_lux")
            or raw.get("ldr")
        ),
        "temp": (
            raw.get("temp")
            or raw.get("temperature")
            or raw.get("temperature_c")
        ),
        "humidity": (
            raw.get("humidity")
            or raw.get("hum")
            or raw.get("humidity_pct")
        ),
        "pump": raw.get("pump", 0),
        "condition": raw.get("condition", "ok"),
    }

    # Validate required fields
    missing = [
        k
        for k in ("soil", "light", "temp", "humidity")
        if data.get(k) is None
    ]
    if missing:
        return {"error": f"missing fields: {', '.join(missing)}"}, 400

    insert_row(data)
    return {"ok": True}, 200


@app.get("/export-csv")
def export_csv():
    con = sqlite3.connect(DB)
    cur = con.cursor()
    cur.execute(
        """
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
        """
    )
    rows = cur.fetchall()
    con.close()

    out = io.StringIO()
    w = csv.writer(out)
    w.writerow(
        [
            "timestamp",
            "soil_moisture",
            "light_level",
            "temperature",
            "humidity",
            "pump_state",
            "condition",
        ]
    )
    w.writerows(rows)

    return Response(
        out.getvalue(),
        mimetype="text/csv",
        headers={
            "Content-Disposition": (
                "attachment; filename=plant_data_for_ei.csv"
            )
        },
    )


if __name__ == "__main__":
    # Running via `python app.py`
    ensure_schema()
    app.run(host="0.0.0.0", port=5000)
