/******************************************************
 * Plant Buddy – ESP32 all-in-one demo
 * Wiring matches our “Official Connections v2”
 * Board: ESP32 Dev Module (30-pin)
 ******************************************************/

#include <Wire.h>
#include <Adafruit_BME680.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// -------- Pin Map --------
// I2C pins used for LCD and BME680 (explicitly chosen for this board)
// On many ESP32 boards these are the default SDA/SCL pins but we set them
// explicitly with Wire.begin(SDA, SCL) below so the wiring is clear.
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;

// Analog sensors: soil moisture probe and LDR (light sensor)
// Note: ADC pins 34 and 35 on many ESP32 variants are input-only (ADC1)
static const int PIN_SOIL_ADC = 34; // ADC1 only-input
static const int PIN_LDR_ADC = 35;  // ADC1 only-input

// Relay control pin (drives a transistor or relay module). Many small 3-pin
// relay modules are active-LOW, meaning writing LOW energizes the relay.
static const int PIN_RELAY = 17; // Active-LOW on many 3-pin modules

// DHT22 temperature/humidity sensor data pin. If using a bare DHT module
// add a pull-up (10k) from data to 3.3V as noted.
static const int PIN_DHT = 27; // DHT22 data (with 10k to 3V3 if bare sensor)
#define DHTTYPE DHT22

// Simple user interface: buzzer and two LEDs (red/green)
// Buzzer is driven as a digital on/off (not PWM) for short beeps.
static const int PIN_BUZZ = 15;    // Active buzzer (on/off)
static const int PIN_LED_RED = 16; // Error / attention
static const int PIN_LED_GRN = 4;  // OK status

// -------- LCD / BME680 I2C --------
// I2C device addresses and LCD geometry
// Many inexpensive I2C LCD backpacks use either 0x27 or 0x3F. If the LCD
// doesn't initialize, try swapping this value to 0x3F.
static const uint8_t LCD_ADDR = 0x27; // change to 0x3F if your module uses that
static const uint8_t LCD_COLS = 16;
static const uint8_t LCD_ROWS = 2;

// BME680 I2C address when SDO is tied to GND (common wiring)
static const uint8_t BME680_ADDR = 0x76; // SDO→GND

// Peripheral instances
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
Adafruit_BME680 bme;       // BME680 communicates over I2C
DHT dht(PIN_DHT, DHTTYPE); // DHT sensor on specified data pin

// -------- App Config --------
// Application tuning constants. Adjust to match your hardware and needs.
// SOIL_DRY_THRESHOLD: threshold for deciding when soil is "dry". ADC values
// are 0..4095 with analogReadResolution(12) below. Many capacitive soil
// sensors return LOWER values when wetter; test your sensor to pick a value.
static const int SOIL_DRY_THRESHOLD = 1800; // 0-4095 (tune for your probe & 3.3V)

// WATER_MS: how long to turn the pump on when watering (milliseconds).
static const int WATER_MS = 3000; // pump ON duration

// Minimum time between pump activations to avoid over-watering / pump wear.
static const long WATER_COOLDOWN_MS = 60L * 1000L; // min time between pump runs

// Read intervals
// How often to perform sensor reads and UI updates (milliseconds)
static const unsigned long READ_MS = 2000;

// Relay polarity (many boards are active-LOW)
// Whether the relay module is active LOW (true for many modules). This
// toggles how setRelay() writes the pin to turn the pump on/off.
static const bool RELAY_ACTIVE_LOW = true;

// -------- State --------
// Runtime state tracking for timing
unsigned long lastReadMs = 0;        // last time we sampled sensors/UI
unsigned long lastWaterActionMs = 0; // last time we ran the pump

void setRelay(bool on)
{
  // Abstract away relay polarity so callers pass a logical 'on' flag.
  // If the relay is active-LOW, writing LOW energizes it; otherwise HIGH does.
  if (RELAY_ACTIVE_LOW)
  {
    digitalWrite(PIN_RELAY, on ? LOW : HIGH);
  }
  else
  {
    digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  }
}

void beep(int ms = 150)
{
  // Simple blocking beep using the buzzer pin. Good for brief signals.
  digitalWrite(PIN_BUZZ, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZ, LOW);
}

void ledsOK()
{
  // Show green LED = OK
  digitalWrite(PIN_LED_GRN, HIGH);
  digitalWrite(PIN_LED_RED, LOW);
}
void ledsERR()
{
  // Show red LED = error/attention
  digitalWrite(PIN_LED_GRN, LOW);
  digitalWrite(PIN_LED_RED, HIGH);
}

bool initLCD()
{
  // Initialize the I2C LCD and show a short boot message. Many I2C LCD
  // backpacks don't provide a way to detect presence before init(), so if
  // the LCD address is wrong you'll typically see garbage or no output.
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Plant Buddy");
  lcd.setCursor(0, 1);
  lcd.print("Init...");
  return true;
}

bool initBME680()
{
  // Try to initialize the BME680 on the expected I2C address. If the device
  // is not present this returns false and callers can decide how to proceed.
  if (!bme.begin(BME680_ADDR))
  {
    return false;
  }

  // Configure recommended oversampling and filter settings for reasonable
  // accuracy without excessive I2C or CPU load.
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  // Disable or lower gas sensor heater (not needed for basic temp/humidity)
  bme.setGasHeater(0, 0); // 0 = off
  return true;
}

void setup()
{
  // Basic pin modes
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false); // OFF at boot

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GRN, OUTPUT);
  ledsERR(); // red on until init passes

  pinMode(PIN_BUZZ, OUTPUT);
  digitalWrite(PIN_BUZZ, LOW);

  // ADC setup
  analogReadResolution(12); // 0..4095
  // Optionally: analogSetAttenuation(ADC_11db); // Wider range, default is fine

  // I2C on explicit pins
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // Serial for debugging
  Serial.begin(115200);
  delay(100);

  // Start peripherals
  bool lcdOK = initLCD();

  dht.begin();
  bool bmeOK = initBME680();

  if (lcdOK)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LCD OK ");
    lcd.print(lcdOK ? "Y" : "N");
    lcd.setCursor(0, 1);
    lcd.print("BME680 ");
    lcd.print(bmeOK ? "OK" : "FAIL");
  }

  // Boot beep
  beep(120);

  // LED status
  if (bmeOK)
    ledsOK();
  else
    ledsERR();

  lastReadMs = millis();
}

struct Readings
{
  float tempC;
  float humidity;
  float pressure_hPa;
  int soilRaw;
  int ldrRaw;
  float dhtTempC;
  float dhtHum;
  bool bmeOK;
  bool dhtOK;
};

Readings readAll()
{
  Readings r{};
  // Soil & LDR analog
  r.soilRaw = analogRead(PIN_SOIL_ADC);
  r.ldrRaw = analogRead(PIN_LDR_ADC);

  // BME680
  r.bmeOK = bme.performReading();
  if (r.bmeOK)
  {
    r.tempC = bme.temperature;              // °C
    r.humidity = bme.humidity;              // %
    r.pressure_hPa = bme.pressure / 100.0f; // hPa
  }

  // DHT
  r.dhtTempC = dht.readTemperature();
  r.dhtHum = dht.readHumidity();
  r.dhtOK = !(isnan(r.dhtTempC) || isnan(r.dhtHum));

  return r;
}

void showOnLCD(const Readings &r)
{
  // Two-line snapshot, fits 16x2
  // L1: So:#### L:####
  // L2: T:##.#C H:##%
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "So:%4d L:%4d", r.soilRaw, r.ldrRaw);

  float tShow = r.bmeOK ? r.tempC : (r.dhtOK ? r.dhtTempC : NAN);
  float hShow = r.bmeOK ? r.humidity : (r.dhtOK ? r.dhtHum : NAN);

  if (isnan(tShow) || isnan(hShow))
  {
    snprintf(line2, sizeof(line2), "T:--.-C H:--%%");
  }
  else
  {
    snprintf(line2, sizeof(line2), "T:%4.1fC H:%2.0f%%", tShow, hShow);
  }

  lcd.setCursor(0, 0);
  lcd.print(line1);
  int pad1 = LCD_COLS - strlen(line1);
  for (int i = 0; i < pad1; i++)
    lcd.print(' ');

  lcd.setCursor(0, 1);
  lcd.print(line2);
  int pad2 = LCD_COLS - strlen(line2);
  for (int i = 0; i < pad2; i++)
    lcd.print(' ');
}

void maybeWater(const Readings &r)
{
  unsigned long now = millis();

  // Simple rule: if soil is "dry" numerically below threshold (or above, depending on your sensor),
  // pump for WATER_MS, then wait WATER_COOLDOWN_MS before allowing again.
  // NOTE: Many capacitive sensors give LOWER values when wetter. Adjust threshold empirically.
  // Determine dryness. Depending on your sensor 'dry' may correspond to higher
  // or lower ADC values; this code assumes 'larger value = drier'. Flip if needed.
  bool isDry = (r.soilRaw > SOIL_DRY_THRESHOLD); // flip sign if your sensor is reversed

  // Status LED: green when OK, red when "dry"
  if (isDry)
  {
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_GRN, LOW);
  }
  else
  {
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_GRN, HIGH);
  }

  if (isDry && (now - lastWaterActionMs >= WATER_COOLDOWN_MS))
  {
    // Start watering
    Serial.println(F("[PUMP] ON"));
    setRelay(true);
    // Short confirmation chirp
    beep(60);

    delay(WATER_MS);

    setRelay(false);
    Serial.println(F("[PUMP] OFF"));
    lastWaterActionMs = millis();
  }
}

void loop()
{
  unsigned long now = millis();
  if (now - lastReadMs >= READ_MS)
  {
    lastReadMs = now;

    Readings r = readAll();

    // Debug to Serial
    Serial.print(F("Soil="));
    Serial.print(r.soilRaw);
    Serial.print(F("  LDR="));
    Serial.print(r.ldrRaw);
    Serial.print(F("  BME(ok="));
    Serial.print(r.bmeOK);
    Serial.print(F(") T="));
    Serial.print(r.tempC, 1);
    Serial.print(F("C H="));
    Serial.print(r.humidity, 0);
    Serial.print(F("% P="));
    Serial.print(r.pressure_hPa, 1);
    Serial.print(F("  DHT(ok="));
    Serial.print(r.dhtOK);
    Serial.print(F(") T="));
    Serial.print(r.dhtTempC, 1);
    Serial.print(F("C H="));
    Serial.print(r.dhtHum, 0);
    Serial.println(F("%"));

    showOnLCD(r);
    maybeWater(r);
  }
}
