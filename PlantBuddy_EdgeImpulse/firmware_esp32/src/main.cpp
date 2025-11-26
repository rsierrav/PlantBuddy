/******************************************************
 * Plant Buddy – ESP32 all-in-one demo
 * Wiring matches our “Official Connections v2”
 * Board: ESP32 Dev Module (30-pin)
 ******************************************************/
#define CLEAN_SERIAL

#include <Wire.h>
#include <Adafruit_BME680.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>  // *** NEW: BH1750 light sensor ***
#include <WiFi.h>
#include <HTTPClient.h>

// -------- Pin Map --------
// I2C pins used for LCD and BME680 (explicitly chosen for this board)
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;

// Analog sensors: soil moisture probe
// Note: we no longer use an analog LDR, BH1750 is I2C.
static const int PIN_SOIL_ADC = 34; // ADC1 only-input
// static const int PIN_LDR_ADC = 35;  // *** LDR no longer used ***

// Relay control pin
static const int PIN_RELAY = 17; // Active-LOW on many 3-pin modules

// DHT22 temperature/humidity sensor
static const int PIN_DHT = 27; // DHT22 data
#define DHTTYPE DHT22

// Simple user interface: buzzer and two LEDs
static const int PIN_BUZZ = 15;
static const int PIN_LED_RED = 16;
static const int PIN_LED_GRN = 4;

// -------- LCD / BME680 I2C --------
static const uint8_t LCD_ADDR = 0x27;
static const uint8_t LCD_COLS = 16;
static const uint8_t LCD_ROWS = 2;

// BME680 I2C address when SDO is tied to GND
static const uint8_t BME680_ADDR = 0x76;

// Peripheral instances
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
Adafruit_BME680 bme;       // BME680 over I2C
DHT dht(PIN_DHT, DHTTYPE); // DHT sensor
BH1750 lightMeter;         // *** NEW: BH1750 instance ***

// -------- App Config --------
static const int SOIL_DRY_THRESHOLD = 1800; // tune for your probe & 3.3V
static const int WATER_MS = 3000;           // pump ON duration (ms)
static const long WATER_COOLDOWN_MS = 60L * 1000L; // min time between watering
static const unsigned long READ_MS = 2000;  // sensor/UI update interval
static const bool RELAY_ACTIVE_LOW = true;

// -------- Wi-Fi --------
const char *WIFI_SSID = "arrozconhuevo";
const char *WIFI_PASS = "creek7527flight";
const char *SERVER_URL = "http://192.168.1.173:5000/ingest";

// -------- State --------
unsigned long lastReadMs = 0;
unsigned long lastWaterActionMs = 0;
bool pumpState = false;

// Edge Impulse integration stub
#ifdef __cplusplus
struct Readings; // forward-declare
#endif
#ifdef USE_EDGE_IMPULSE
// #include <edge-impulse-sdk/classifier/ei_run_classifier.h>
bool run_edge_impulse_classifier(const Readings &r, String &out_label, float &out_conf)
{
  out_label = "unknown";
  out_conf = 0.0f;
  return false;
}
#else
bool run_edge_impulse_classifier(const Readings &r, String &out_label, float &out_conf)
{
  (void)r;
  out_label = "none";
  out_conf = 0.0f;
  return false;
}
#endif

void setRelay(bool on)
{
  if (RELAY_ACTIVE_LOW)
    digitalWrite(PIN_RELAY, on ? LOW : HIGH);
  else
    digitalWrite(PIN_RELAY, on ? HIGH : LOW);

  pumpState = on;
}

void beep(int ms = 150)
{
  digitalWrite(PIN_BUZZ, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZ, LOW);
}

void ledsOK()
{
  digitalWrite(PIN_LED_GRN, HIGH);
  digitalWrite(PIN_LED_RED, LOW);
}
void ledsERR()
{
  digitalWrite(PIN_LED_GRN, LOW);
  digitalWrite(PIN_LED_RED, HIGH);
}

bool initLCD()
{
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
  if (!bme.begin(BME680_ADDR))
    return false;

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(0, 0); // off
  return true;
}

void setup()
{
  // Basic pin modes
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false); // OFF at boot

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GRN, OUTPUT);
  ledsERR(); // red until init passes

  pinMode(PIN_BUZZ, OUTPUT);
  digitalWrite(PIN_BUZZ, LOW);

  // ADC setup (soil only)
  analogReadResolution(12); // 0..4095

  // Serial for debugging (start this first)
  Serial.begin(115200);
  delay(100);

  // I2C on explicit pins
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // *** NEW: init BH1750 (ADDR floating -> address 0x23) ***
  bool lightOK = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
#ifndef CLEAN_SERIAL
  if (!lightOK) {
    Serial.println("BH1750 init failed!");
  }
#endif

  // Start peripherals
  bool lcdOK = initLCD();

  dht.begin();
  bool bmeOK = initBME680();

#ifndef CLEAN_SERIAL
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
#endif

#ifndef CLEAN_SERIAL
  beep(120);
#endif

  if (bmeOK)
    ledsOK();
  else
    ledsERR();

  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  lastReadMs = millis();
}

// *** UPDATED: light is now lux (BH1750), not LDR ADC ***
struct Readings
{
  float tempC;
  float humidity;
  float pressure_hPa;
  int   soilRaw;
  float lux;       // <- BH1750 lux
  float dhtTempC;
  float dhtHum;
  bool  bmeOK;
  bool  dhtOK;
};

Readings readAll()
{
  Readings r{};

  // Soil analog
  r.soilRaw = analogRead(PIN_SOIL_ADC);

  // *** NEW: BH1750 light in lux ***
  float lux = lightMeter.readLightLevel(); // returns lux, or <0 on error
  if (lux < 0) {
    lux = 0.0f; // fallback if sensor error
  }
  r.lux = lux;

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
  // L1: So:#### L:####
  // L2: T:##.#C H:##%
  char line1[17], line2[17];

  // *** CHANGED: display lux (rounded to integer) instead of raw ADC ***
  snprintf(line1, sizeof(line1), "So:%4d L:%4.0f", r.soilRaw, r.lux);

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

// Print a single CSV line suitable for Edge Impulse data forwarder.
// Order: soil, light(lux), temp, humidity, pump_state
void printForEdgeImpulse(const Readings &r)
{
  float temp = 0.0f;
  float hum = 0.0f;

  if (r.bmeOK)
  {
    temp = r.tempC;
    hum = r.humidity;
  }
  else if (r.dhtOK)
  {
    temp = r.dhtTempC;
    hum = r.dhtHum;
  }
  else
  {
    temp = 0.0f;
    hum = 0.0f;
  }

  // *** CHANGED: second field is lux instead of LDR ADC ***
  Serial.print(r.soilRaw);
  Serial.print(',');
  Serial.print(r.lux, 2);
  Serial.print(',');
  Serial.print(temp, 2);
  Serial.print(',');
  Serial.print(hum, 2);
  Serial.print(',');
  Serial.println(pumpState ? 1 : 0);
}

void maybeWater(const Readings &r)
{
  unsigned long now = millis();

  bool isDry = (r.soilRaw > SOIL_DRY_THRESHOLD); // adjust if sensor reversed

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
#ifndef CLEAN_SERIAL
    Serial.println(F("[PUMP] ON"));
#endif
    setRelay(true);
    beep(60);

    delay(WATER_MS);
    setRelay(false);
#ifndef CLEAN_SERIAL
    Serial.println(F("[PUMP] OFF"));
#endif
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

    String label;
    float confidence = 0.0f;
    bool didInfer = run_edge_impulse_classifier(r, label, confidence);
    (void)didInfer;

    // Send CSV for Edge Impulse
    printForEdgeImpulse(r);

    // UI + watering logic
    showOnLCD(r);
    maybeWater(r);

    if (WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;
      http.begin(SERVER_URL);
      http.addHeader("Content-Type", "application/json");

      String payload = "{";
      payload += "\"soil\":" + String(r.soilRaw) + ",";
      payload += "\"light\":" + String(r.ldrRaw) + ",";
      payload += "\"temp\":" + String(r.tempC, 2) + ",";
      payload += "\"humidity\":" + String(r.humidity, 2) + ",";
      payload += "\"pump\":" + String((digitalRead(PIN_RELAY) == LOW) ? 1 : 0) + ",";
      payload += "\"condition\":\"" + String((r.soilRaw > SOIL_DRY_THRESHOLD) ? "dry" : "ok") + "\"";
      payload += "}";

      int code = http.POST(payload);
      Serial.printf("POST /ingest -> code %d\n", code);
      http.end();
    }
  }
}
