# PlantBuddy (ESP32 + PlatformIO)

IoT watering/monitoring system using an ESP32, LCD1602 (I2C), soil moisture sensor, BME680, BH1750 light sensor, DHT22, and a 5V relay + pump. Built with PlatformIO (Arduino framework).

---

## Quick Start

### Prerequisites

- [VS Code](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- [Git](https://git-scm.com/downloads)
- USB driver for your ESP32 board:
  - **CP2102**: [Silicon Labs CP210x VCP driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
  - **CH340**: [CH34x driver](https://github.com/WCHSoftGroup/ch34xser)

### Clone the Project

````bash
git clone https://github.com/rsierrav/PlantBuddy.git
cd PlantBuddy
````
### Build and Upload

1. Open the folder in VS Code.
2. Make sure `platformio.ini` looks like this:

    ```ini
    [platformio]
    default_envs = esp32dev

    [env:esp32dev]
    platform = espressif32
    board = esp32dev         ; DOIT ESP32 DEVKIT V1
    framework = arduino
    upload_port  = COM3      ; adjust if needed
    monitor_port = COM3
    monitor_speed = 115200
    ```

3. Connect ESP32 via USB.
4. Build (✓) → Upload (→).
5. If it hangs on “Connecting…”, hold BOOT while upload begins, then release.
6. Open Serial Monitor (plug icon) at 115200 baud.

---

## Sanity Check (PlantBuddy firmware)

The current `src/main.cpp` will:

- Read soil, light, temperature, and humidity.
- Show soil + light on the first line of the LCD, temp + humidity on the second line.
- Control a relay + pump when soil is “dry” (above a configurable threshold).
- Print CSV lines to Serial like:

  ```text
  1534,550.00,28.90,43.16,0
  ```

If you see stable sensor values on the LCD and CSV output in the Serial Monitor at `115200` baud, the firmware is working.

---

## Project Structure

````
.
├── include/ # headers
├── lib/ # custom libraries
├── src/
│ └── main.cpp # application entry
├── test/ # PlatformIO unit tests
├── platformio.ini # board/config
└── README.md
````

---

## Collaboration Workflow

- Use branches + PRs (simpler than forks for small teams).

### Workflow

```bash
# Pull latest
git checkout main
git pull origin main

# Create a feature branch
git checkout -b feat/lcd-i2c

# Work, commit, push
git add .
git commit -m "feat(lcd): add hello world message"
git push -u origin feat/lcd-i2c
````

- Open a Pull Request on GitHub, request review, then merge.

#### Branch naming

- `feat/...` – new features
- `fix/...` – bug fixes
- `docs/...` – documentation
- `chore/...` – repo maintenance

#### Fork workflow (optional)

- Teammates can fork → push branches → open PRs from their fork.
- For trusted teammates, branch workflow is easier.

---

## 🔌 Hardware Pin Map

| Component        | ESP32 Pin      | Notes                          |
|------------------|----------------|--------------------------------|
| I2C bus (LCD + BME680 + BH1750) | SDA = 21    | Shared I2C bus                |
|                  | SCL = 22       |                                |
| Soil moisture    | GPIO 34 (ADC1) | Analog input (0–4095)          |
| DHT22            | GPIO 27        | Temp/humidity backup sensor    |
| Relay IN         | GPIO 17        | Active LOW in firmware         |
| Buzzer           | GPIO 15        | Simple beep feedback           |
| Status LED (red) | GPIO 16        | Dry / error indicator          |
| Status LED (grn) | GPIO 4         | OK indicator                   |
| Onboard LED      | GPIO 2         | Not used by current firmware   |

All modules must share a common GND.

---

## Library Dependencies

Add automatically via PlatformIO on build, or specify in `platformio.ini`:

```ini
lib_deps =
  marcoschwartz/LiquidCrystal_I2C@^1.1.4
  adafruit/Adafruit BME680 Library@^2.0.4
  adafruit/Adafruit Unified Sensor@^1.1.14
```

---

## Troubleshooting

- **Stuck on “Connecting…”**  
  Hold BOOT while upload starts. Try another USB cable/port.

- **Serial Monitor shows gibberish**  
  Set baud to 115200 in both code and `platformio.ini`.

- **Arduino.h red squiggle**  
  Run PlatformIO: Rebuild IntelliSense Index. Ignore if build/upload works.

- **No COM port**  
  Install correct USB driver (CP210x / CH34x). Check Device Manager (Windows).

---

## Edge Impulse & Data Forwarding

### Firmware Serial Output (CSV)

The ESP32 PlantBuddy firmware prints one **comma-separated line per sample** at `115200` baud, example:

```text
1534,550.00,28.90,43.16,0
```

Field order:

1. `soil` – raw ADC from soil moisture probe (0–4095)
2. `light` – BH1750 lux reading (float)
3. `temp` – temperature (°C), from BME680 or DHT22 fallback
4. `humidity` – relative humidity (%)
5. `pump_state` – `0` = off, `1` = on

This format is designed for the **Edge Impulse Data Forwarder**.

### Using the Edge Impulse Data Forwarder

With the ESP32 firmware flashed and running, and the Serial Monitor **closed**:

```powershell
edge-impulse-data-forwarder.cmd --frequency 0.5
```

* Log in with your Edge Impulse account.

* Give the device a name (example: `PlantBuddy-1`).

* When asked for axis names, enter:

  ```text
  soil,light,temp,humidity
  ```

* When asked if a timestamp is included: answer `N`.

* Frequency: `0.5` Hz (the firmware samples once every 2000 ms).

After this, the device shows up under **Devices** in your Edge Impulse project and you can start sampling labeled data in **Data Acquisition**.

---

## Local Server (Optional)

A small Flask server is included under `PlantBuddy_EdgeImpulse/server` to log samples locally.

* `server/app.py` – Flask app for storing data in SQLite (`plantbuddy.db`).
* `server/serial_forwarder.py` – reads CSV from the ESP32 and posts JSON to the Flask server.

### Endpoints

* `POST /ingest` — ingest sensor samples (fields: `soil`, `light`, `temp`, `humidity`, optional `pump_state`, `condition`).
* `POST /label` — add or update labels/conditions for stored samples.
* `GET /export-csv` — export raw CSV with timestamp + pump state.
* `GET /export-ei-csv` — Edge-Impulse-friendly CSV: `soil,light,temp,humidity,label`.

### Run the server

```bash
cd PlantBuddy_EdgeImpulse/server
python -m venv .venv
# Windows:
.venv\Scripts\activate
# macOS/Linux:
source .venv/bin/activate

pip install -r requirements.txt
python app.py
```

The server listens on port `5000` by default.

### Notes / Best practices

- Calibrate soil probe: record a few wet/medium/dry readings and choose thresholds.
- When you deploy an Edge Impulse model, add the library to `firmware_esp32/lib/` and define `USE_EDGE_IMPULSE` in `platformio.ini` build flags so the classifier code is included.
- Keep pump activations short and use a cooldown (`WATER_COOLDOWN_MS`) to avoid over-watering.
- For training data: use `POST /label` to store labeled samples, then `GET /export-ei-csv` to download a CSV ready for Edge Impulse.
