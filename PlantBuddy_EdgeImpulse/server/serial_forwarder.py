#!/usr/bin/env python3
"""
Simple serial -> HTTP forwarder for PlantBuddy JSON lines.
Reads one JSON object per line from a serial port and POSTs it to the
local server `/ingest` endpoint. Useful if you prefer archiving samples
locally rather than streaming directly into Edge Impulse.

Usage:
  python serial_forwarder.py --port COM3 --baud 115200 --url http://127.0.0.1:5000/ingest

Requirements:
  pip install -r requirements.txt

"""

import argparse
import json
import time
import requests
import serial
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Serial -> HTTP forwarder for PlantBuddy")
    parser.add_argument("--port",
                        required=True,
                        help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud",
                        type=int,
                        default=115200,
                        help="Serial baud rate")
    parser.add_argument("--url",
                        default="http://127.0.0.1:5000/ingest",
                        help="HTTP ingest endpoint")
    parser.add_argument("--retry",
                        type=float,
                        default=2.0,
                        help="Seconds to wait before reconnect attempts")
    args = parser.parse_args()

    print(f"Opening serial port {args.port} at {args.baud} bps")

    while True:
        try:
            with serial.Serial(args.port, args.baud, timeout=1) as ser:
                print("Serial opened — reading lines. Press Ctrl-C to stop.")
                buffer = b""
                while True:
                    try:
                        line = ser.readline()
                        if not line:
                            continue
                        try:
                            text = line.decode("utf-8").strip()
                        except Exception:
                            # ignore undecodable lines
                            continue

                        # Some firmware prints human messages; skip lines that are not JSON
                        if not text.startswith("{"):
                            # optionally print for debugging
                            print("SKIP:", text)
                            continue

                        try:
                            obj = json.loads(text)
                        except Exception:
                            print("Invalid JSON, skipping:", text)
                            continue

                        # Normalize to the server's expected keys if needed
                        payload = {
                            "soil": obj.get("soil"),
                            "light": obj.get("light"),
                            "temp": obj.get("temp"),
                            "humidity": obj.get("humidity"),
                            "pump": obj.get("pump_state") or obj.get("pump")
                            or 0,
                            "condition": obj.get("condition", ""),
                        }

                        # Post to server
                        try:
                            r = requests.post(args.url,
                                              json=payload,
                                              timeout=5)
                            if r.status_code == 200:
                                print("POST OK", payload)
                            else:
                                print("POST failed", r.status_code, r.text)
                        except Exception as e:
                            print("Error posting to server:", e)

                    except KeyboardInterrupt:
                        print("Interrupted by user — closing.")
                        return
                    except Exception as e:
                        print("Serial read loop error:", e)
                        break
        except serial.SerialException as e:
            print("Could not open serial port:", e)
            print(f"Retrying in {args.retry} seconds...")
            time.sleep(args.retry)
        except KeyboardInterrupt:
            print("Interrupted — exiting.")
            return


if __name__ == "__main__":
    main()
