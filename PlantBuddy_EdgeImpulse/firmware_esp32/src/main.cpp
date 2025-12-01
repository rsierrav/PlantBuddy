/******************************************************
 * Plant Buddy – ESP32 all-in-one demo (Edge Impulse–Safe Version)
 * Includes: BME680, BH1750, Soil Moisture ADC, DHT22,
 * LCD, Pump Relay, Wi-Fi JSON POST, and EI CSV Output
 ******************************************************/
// #define CLEAN_SERIAL // Uncomment to enable CSV output for Edge Impulse data collection

#include <Wire.h>
#include <Adafruit_BME680.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Plant_Buddy_inferencing.h"

// -------- Pin Map --------
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;

static const int PIN_SOIL_ADC = 34; // ADC1 only-input

static const int PIN_RELAY = 17; // Active-LOW
static const int PIN_DHT = 27;
#define DHTTYPE DHT22

static const int PIN_BUZZ = 15;
static const int PIN_LED_RED = 16;
static const int PIN_LED_GRN = 4;

// -------- LCD / BME680 I2C --------
static const uint8_t LCD_ADDR = 0x27;
static const uint8_t LCD_COLS = 16;
static const uint8_t LCD_ROWS = 2;

static const uint8_t BME680_ADDR = 0x76;

// Peripheral instances
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
Adafruit_BME680 bme;
DHT dht(PIN_DHT, DHTTYPE);
BH1750 lightMeter;

// -------- Config --------
static const int SOIL_DRY_THRESHOLD = 1800;
static const int WATER_MS = 3000;
static const long WATER_COOLDOWN_MS = 60L * 1000L;
static const unsigned long READ_MS = 2000;
static const bool RELAY_ACTIVE_LOW = true;

// -------- Wi-Fi --------
const char *WIFI_SSID = "arrozconhuevo";
const char *WIFI_PASS = "creek7527flight";
const char *SERVER_URL = "http://192.168.1.173:5000/ingest";

// -------- State --------
unsigned long lastReadMs = 0;
unsigned long lastWaterActionMs = 0;
bool pumpState = false;

// ====== SANITIZATION FUNCTIONS (Fix NaN Issues) ======
int safeAnalogRead(int pin)
{
  int v = analogRead(pin);
  if (isnan(v) || v < 0 || v > 4095)
  {
    delay(5);
    v = analogRead(pin);
  }
  if (isnan(v) || v < 0)
    return 0;
  if (v > 4095)
    return 4095;
  return v;
}

float safeFloat(float x)
{
  return (isnan(x) || isinf(x)) ? 0.0f : x;
}

// -------- Pump Relay --------
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
  bme.setGasHeater(0, 0);
  return true;
}

struct Readings
{
  float tempC;
  float humidity;
  float pressure_hPa;
  int soilRaw;
  float lux;
  float dhtTempC;
  float dhtHum;
  bool bmeOK;
  bool dhtOK;
};

Readings readAll()
{
  Readings r{};

  r.soilRaw = safeAnalogRead(PIN_SOIL_ADC);

  float lux = lightMeter.readLightLevel();
  if (lux < 0)
    lux = 0;
  r.lux = safeFloat(lux);

  r.bmeOK = bme.performReading();
  if (r.bmeOK)
  {
    r.tempC = safeFloat(bme.temperature);
    r.humidity = safeFloat(bme.humidity);
    r.pressure_hPa = safeFloat(bme.pressure / 100.0f);
  }

  r.dhtTempC = safeFloat(dht.readTemperature());
  r.dhtHum = safeFloat(dht.readHumidity());
  r.dhtOK = !(isnan(r.dhtTempC) || isnan(r.dhtHum));

  return r;
}

void showOnLCD(const Readings &r)
{
  char line1[17], line2[17];

  snprintf(line1, sizeof(line1), "So:%4d L:%4.0f", r.soilRaw, r.lux);

  snprintf(line2, sizeof(line2), "T:%4.1fC H:%2.0f%%",
           safeFloat(r.tempC), safeFloat(r.humidity));

  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// ====== Edge Impulse CSV OUTPUT (Sanitized) ======
void printForEdgeImpulse(const Readings &r)
{
  float temp = r.bmeOK ? r.tempC : (r.dhtOK ? r.dhtTempC : 0.0f);
  float hum = r.bmeOK ? r.humidity : (r.dhtOK ? r.dhtHum : 0.0f);

  Serial.print(safeAnalogRead(PIN_SOIL_ADC));
  Serial.print(',');
  Serial.print(safeFloat(r.lux), 2);
  Serial.print(',');
  Serial.print(safeFloat(temp), 2);
  Serial.print(',');
  Serial.print(safeFloat(hum), 2);
  Serial.print(',');
  Serial.println(pumpState ? 1 : 0);
}

void maybeWater(const Readings &r)
{
  unsigned long now = millis();

  bool isDry = (r.soilRaw > SOIL_DRY_THRESHOLD);

  digitalWrite(PIN_LED_RED, isDry ? HIGH : LOW);
  digitalWrite(PIN_LED_GRN, isDry ? LOW : HIGH);

  if (isDry && (now - lastWaterActionMs >= WATER_COOLDOWN_MS))
  {
    setRelay(true);
    beep(60);
    delay(WATER_MS);
    setRelay(false);
    lastWaterActionMs = millis();
  }
}

// ====== Edge Impulse Classifier Integration ======
//
// Model expects 5 inputs in this order:
//   [soil, light, temp, humidity, pump_state]
//
void run_edge_impulse_classifier(float soil,
                                 float light,
                                 float temp,
                                 float hum,
                                 float pump_state)
{
    // Sanity check
    if (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE != 5) {
#ifndef CLEAN_SERIAL
        Serial.print("ERROR: Model expects ");
        Serial.print(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
        Serial.println(" features, but code assumes 5.");
#endif
        return;
    }

    float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

    // Feature order must match Edge Impulse model:
    // soil, light, temp, humidity, pump_state
    features[0] = safeFloat(soil);
    features[1] = safeFloat(light);
    features[2] = safeFloat(temp);
    features[3] = safeFloat(hum);
    features[4] = safeFloat(pump_state);

    // Wrap the buffer in an Edge Impulse signal_t
    signal_t signal;
    int err = numpy::signal_from_buffer(
        features,
        EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE,
        &signal
    );
    if (err != 0) {
#ifndef CLEAN_SERIAL
        Serial.print("signal_from_buffer failed: ");
        Serial.println(err);
#endif
        return;
    }

    // Run the classifier
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR ei_err = run_classifier(
        &signal,
        &result,
        /* debug = */ false
    );

    if (ei_err != EI_IMPULSE_OK) {
#ifndef CLEAN_SERIAL
        Serial.print("run_classifier failed: ");
        Serial.println(ei_err);
#endif
        return;
    }

    // Pick highest-confidence class
    size_t best_i = 0;
    float best_val = 0.0f;

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value > best_val) {
            best_val = result.classification[i].value;
            best_i = i;
        }
    }

#ifndef CLEAN_SERIAL
    Serial.print("Predicted: ");
    Serial.print(result.classification[best_i].label);
    Serial.print(" (");
    Serial.print(best_val, 2);
    Serial.println(")");
#endif
}

void setup()
{
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GRN, OUTPUT);
  ledsERR();

  pinMode(PIN_BUZZ, OUTPUT);
  digitalWrite(PIN_BUZZ, LOW);

  analogReadResolution(12);

  Serial.begin(115200);
  delay(100);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  bool lightOK = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  bool lcdOK = initLCD();

  dht.begin();
  bool bmeOK = initBME680();

  if (bmeOK)
    ledsOK();

  unsigned long wifiStart = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 10000)
  {
    delay(500);
  }

  lastReadMs = millis();
}

void loop()
{
  unsigned long now = millis();
  if (now - lastReadMs >= READ_MS)
  {
    lastReadMs = now;

    Readings r = readAll();

    // ===== Data collection mode (Edge Impulse CSV) =====
#ifdef CLEAN_SERIAL
    printForEdgeImpulse(r);
#endif

    // LCD + watering logic
    showOnLCD(r);
    maybeWater(r);

    // ===== Inference mode (when CLEAN_SERIAL is *not* defined) =====
#ifndef CLEAN_SERIAL
    // Prepare features for the classifier
    float temp = r.bmeOK ? r.tempC : (r.dhtOK ? r.dhtTempC : 0.0f);
    float hum  = r.bmeOK ? r.humidity : (r.dhtOK ? r.dhtHum : 0.0f);
    float pump_val = pumpState ? 1.0f : 0.0f;

    run_edge_impulse_classifier(
        (float)r.soilRaw,  // soil
        r.lux,             // light
        temp,              // temp
        hum,               // humidity
        pump_val           // pump_state
    );
#endif

    // ------- Wi-Fi JSON POST -------
    if (WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;
      http.begin(SERVER_URL);
      http.addHeader("Content-Type", "application/json");

      float temp = r.bmeOK ? r.tempC : (r.dhtOK ? r.dhtTempC : 0.0f);
      float hum = r.bmeOK ? r.humidity : (r.dhtOK ? r.dhtHum : 0.0f);

      String payload = "{";
      payload += "\"soil\":" + String(r.soilRaw) + ",";
      payload += "\"light\":" + String(r.lux, 2) + ",";
      payload += "\"temp\":" + String(temp, 2) + ",";
      payload += "\"humidity\":" + String(hum, 2) + ",";
      payload += "\"pump_state\":" + String(pumpState ? 1 : 0) + ",";
      payload += "\"condition\":\"" + String((r.soilRaw > SOIL_DRY_THRESHOLD) ? "dry" : "ok") + "\"";
      payload += "}";

      http.POST(payload);
      http.end();
    }
  }
}