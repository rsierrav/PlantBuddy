# PlantBuddy (ESP32 + PlatformIO)

IoT watering/monitoring system using an ESP32, LCD1602 (I2C), BME680, photoresistor, and a 5V relay + pump. Built with PlatformIO (Arduino framework).

---

## Quick Start

### Prerequisites
- [VS Code](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- [Git](https://git-scm.com/downloads)
- USB driver for your ESP32 board:
  - **CP2102**: [Silicon Labs CP210x VCP driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
  - **CH340**: [CH34x driver](https://github.com/WCHSoftGroup/ch34xser)

### Clone the Project
```bash
git clone https://github.com/rsierrav/PlantBuddy.git
cd PlantBuddy

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

## 🟢 Sanity Check (Blink)

The starter code (`src/main.cpp`) will:
- Blink the onboard LED (GPIO 2).
- Print `LED ON` / `LED OFF` to Serial Monitor.

---

## 📂 Project Structure

```
.
├── include/           # headers
├── lib/               # custom libraries
├── src/
│   └── main.cpp       # application entry
├── test/              # PlatformIO unit tests
├── platformio.ini     # board/config
└── README.md
```

---

## 🤝 Collaboration Workflow

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
```

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

| Component         | ESP32 Pin   | Notes                |
|-------------------|-------------|----------------------|
| LCD1602 (I2C)     | SDA = 21    | Shared I2C           |
|                   | SCL = 22    |                      |
| BME680 (I2C)      | SDA = 21    | Same I2C bus         |
|                   | SCL = 22    |                      |
| Photoresistor     | AIN = 34    | With 10kΩ divider    |
| Relay IN          | GPIO 25     | Any output GPIO      |
| Pump              | via relay   | External 5V supply   |
| LED (onboard)     | GPIO 2      | Default sanity blink |

⚠️ Ensure all GNDs are common.

---

## 📚 Library Dependencies

Add automatically via PlatformIO on build, or specify in `platformio.ini`:

```ini
lib_deps =
  marcoschwartz/LiquidCrystal_I2C@^1.1.4
  adafruit/Adafruit BME680 Library@^2.0.4
  adafruit/Adafruit Unified Sensor@^1.1.14
```

---

## 🛠 Troubleshooting

- **Stuck on “Connecting…”**  
  Hold BOOT while upload starts. Try another USB cable/port.

- **Serial Monitor shows gibberish**  
  Set baud to 115200 in both code and `platformio.ini`.

- **Arduino.h red squiggle**  
  Run PlatformIO: Rebuild IntelliSense Index. Ignore if build/upload works.

- **No COM port**  
  Install correct USB driver (CP210x / CH34x). Check Device Manager (Windows).

---

## 📅 Roadmap

- [x] Blink test
- [ ] LCD Hello World
- [ ] Read BME680 values
- [ ] Show values on LCD (rotate pages)
- [ ] Light sensor integration
- [ ] Relay + pump control
- [ ] Soil moisture integration
- [ ] Threshold automation
- [ ] Optional: data logging
