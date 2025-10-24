from flask import Flask, request, Response
import sqlite3
import csv
import io

DB = "plantbuddy.db"
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
        (soil_moisture, light_level, temperature, humidity,
         pump_state, condition)
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


@app.post("/ingest")
def ingest():
    data = request.get_json(force=True)
    insert_row(data)
    return {"ok": True}


@app.get("/export-csv")
def export_csv():
    con = sqlite3.connect(DB)
    cur = con.cursor()
    cur.execute(
        """SELECT timestamp,
               soil_moisture,
               light_level,
               temperature,
               humidity,
               pump_state,
               condition
           FROM plant_data
           ORDER BY timestamp"""
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
            ),
        },
    )


if __name__ == "__main__":
    ensure_schema()
    app.run(host="0.0.0.0", port=5000)
