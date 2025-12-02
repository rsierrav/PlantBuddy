/******************************************************
 * Plant Buddy – ESP32 all-in-one demo (Edge Impulse–Safe Version)
 * Includes: BME680, BH1750, Soil Moisture ADC, DHT22,
 * LCD, Pump Relay, Wi-Fi JSON POST, and EI CSV Output
 ******************************************************/
// #define CLEAN_SERIAL // Uncomment to enable CSV output for Edge Impulse data collection
#include <secrets.h>
#include <Wire.h>
#include <Adafruit_BME680.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "plantBuddy_inferencing.h"
#include "secrets.h"

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
static const int WATER_MS = 3000;
static const long WATER_COOLDOWN_MS = 60L * 1000L;
static const unsigned long READ_MS = 2000;
static const bool RELAY_ACTIVE_LOW = true;
// AI + watering tuning
static const int SOIL_SAFETY_WET = 1600;     // Below this, never water (soil clearly moist)
static const int SOIL_DRY_THRESHOLD = 2100; // at or above this = clearly dry (adjust after testing)
static const float AI_CONF_THRESHOLD = 0.6f; // How sure AI must be to trigger watering

// -------- SUPABASE CONFIG --------
static const char* SUPABASE_URL = "https://lkehixwlfdqsdebixcap.supabase.co/rest/v1/plant_data";
static const char* SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImxrZWhpeHdsZmRxc2RlYml4Y2FwIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjQ2MTk1NDYsImV4cCI6MjA4MDE5NTU0Nn0.HTt0VPEUgbkSJZfvIkuec6P6-TlHKr37c1FLl2hs6Ak";


// -------- State --------
unsigned long lastReadMs = 0;
unsigned long lastWaterActionMs = 0;
bool pumpState = false;

String last_ai_label = "unknown";
float last_ai_conf = 0.0f;


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

// ====== Condition Computation ======
struct ConditionState {
  String label;      // "fine" or "needs_water"   -> for dashboard / logic
  bool warnDry;      // whether LED should be red
  bool shouldWater;  // whether pump is allowed to run
};

// Decide the overall plant state from soil + AI
ConditionState computeCondition(const Readings &r) {
  ConditionState cs;

  bool soilClearlyWet  = (r.soilRaw <= SOIL_SAFETY_WET);
  bool soilMaybeDry    = (r.soilRaw >= SOIL_DRY_THRESHOLD);  // stricter dry
  bool aiSaysDry       = (last_ai_label == "needs_water" &&
                          last_ai_conf >= AI_CONF_THRESHOLD);

  // Default: happy
  cs.label       = "fine";
  cs.warnDry     = false;
  cs.shouldWater = false;

  // 1) WET ZONE: soil clearly wet → plant is happy, AI is ignored
  if (soilClearlyWet) {
    cs.label       = "fine";        // dashboard: happy
    cs.warnDry     = false;         // LED green
    cs.shouldWater = false;         // pump off
  }
  // 2) DRY ZONE: soil clearly dry → ask AI to confirm
  else if (soilMaybeDry) {
    if (aiSaysDry) {
      cs.label       = "needs_water"; // dashboard + LCD say "needs water"
      cs.warnDry     = true;          // LED red
      cs.shouldWater = true;          // pump allowed (will still respect cooldown)
    } else {
      cs.label       = "fine";        // AI doesn't agree → stay safe
      cs.warnDry     = false;
      cs.shouldWater = false;
    }
  }
  // 3) MIDDLE ZONE: kind of moist → call it fine and DO NOT WATER
  else {
    // between 1600 and 2100
    cs.label       = "fine";        // everyone says plant is fine
    cs.warnDry     = false;         // LED green
    cs.shouldWater = false;         // pump off even if AI says dry
  }

  return cs;
}

// ====== LCD DISPLAY ======
void showOnLCD(const Readings &r)
{
  char line1[17], line2[17];

  snprintf(line1, sizeof(line1), "So:%4d L:%4.0f", r.soilRaw, r.lux);

  // Use the same condition as LEDs + pump + dashboard
  ConditionState cs = computeCondition(r);

  const char *status;
  if (cs.label == "needs_water") {
    status = "WATER";
  } else {
    status = "OK";
  }

  snprintf(line2, sizeof(line2), "T:%4.1fC %s",
           safeFloat(r.tempC),
           status);

  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print("                ");
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

// ====== Watering Logic ======
void maybeWater(const Readings &r)
{
  unsigned long now = millis();

  ConditionState cs = computeCondition(r);

  // LEDs from condition
  digitalWrite(PIN_LED_RED, cs.warnDry ? HIGH : LOW);
  digitalWrite(PIN_LED_GRN, cs.warnDry ? LOW  : HIGH);

  // Only water if condition says it's OK AND cooldown passed
  if (cs.shouldWater && (now - lastWaterActionMs >= WATER_COOLDOWN_MS))
  {
    Serial.print("WATERING: soil=");
    Serial.print(r.soilRaw);
    Serial.print(" AI=");
    Serial.print(last_ai_label);
    Serial.print(" conf=");
    Serial.println(last_ai_conf, 2);

    setRelay(true);
    beep(60);
    delay(WATER_MS);
    setRelay(false);
    lastWaterActionMs = millis();
  }
  else
  {
    Serial.print("NO WATER: soil=");
    Serial.print(r.soilRaw);
    Serial.print(" AI=");
    Serial.print(last_ai_label);
    Serial.print(" conf=");
    Serial.println(last_ai_conf, 2);
  }
}

// ====== Edge Impulse Classifier Integration ======
//
// New model expects 3 inputs in this order:
//   [soil, humidity, pump_state]
//
void run_edge_impulse_classifier(float soil,
                                 float hum,
                                 float pump_state)
{
  // Sanity check for the new model
  if (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE != 3)
  {
#ifndef CLEAN_SERIAL
    Serial.print("ERROR: Model expects ");
    Serial.print(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    Serial.println(" features, but code assumes 3.");
#endif
    return;
  }

  float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

  // Feature order must match Edge Impulse model:
  // soil, humidity, pump_state
  features[0] = safeFloat(soil);
  features[1] = safeFloat(hum);
  features[2] = safeFloat(pump_state);

  // Wrap the buffer in an Edge Impulse signal_t
  signal_t signal;
  int err = numpy::signal_from_buffer(
      features,
      EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE,
      &signal);
  if (err != 0)
  {
#ifndef CLEAN_SERIAL
    Serial.print("signal_from_buffer failed: ");
    Serial.println(err);
#endif
    return;
  }

  // Run the classifier
  ei_impulse_result_t result = {0};
  EI_IMPULSE_ERROR ei_err = run_classifier(
      &signal,
      &result,
      /* debug = */ false);

  if (ei_err != EI_IMPULSE_OK)
  {
#ifndef CLEAN_SERIAL
    Serial.print("run_classifier failed: ");
    Serial.println(ei_err);
#endif
    return;
  }

  // Pick highest-confidence class
  size_t best_i = 0;
  float best_val = 0.0f;

  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
  {
    if (result.classification[i].value > best_val)
    {
      best_val = result.classification[i].value;
      best_i = i;
    }
  }

  // Save result for use in JSON / logic
  last_ai_label = String(result.classification[best_i].label);
  last_ai_conf  = best_val;

#ifndef CLEAN_SERIAL
  Serial.print("Predicted: ");
  Serial.print(last_ai_label);
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

  bool enableWiFi = false; // user choice

  Serial.println("Wi-Fi auto-connect starting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 10000)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWi-Fi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\nWi-Fi connection failed. Continuing offline.");
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

    // ===== Inference mode (when CLEAN_SERIAL is *not* defined) =====
#ifndef CLEAN_SERIAL
    float temp = r.bmeOK ? r.tempC : (r.dhtOK ? r.dhtTempC : 0.0f);
    float hum  = r.bmeOK ? r.humidity : (r.dhtOK ? r.dhtHum : 0.0f);
    float pump_val = pumpState ? 1.0f : 0.0f;

    // New model: soil, humidity, pump_state
    run_edge_impulse_classifier(
        (float)r.soilRaw, // soil
        hum,              // humidity
        pump_val          // pump_state
    );
#endif

    // ===== Data collection mode (Edge Impulse CSV) =====
#ifdef CLEAN_SERIAL
    printForEdgeImpulse(r);
#endif

    // LCD + watering logic (now using latest AI prediction)
    showOnLCD(r);
    maybeWater(r);

    // ------- Wi-Fi JSON POST (Supabase) -------
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;

        // Supabase REST endpoint for inserting rows
        http.begin(SUPABASE_URL);

        // Required Supabase headers
        http.addHeader("Content-Type", "application/json");
        http.addHeader("apikey", SUPABASE_KEY);
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
        http.addHeader("Prefer", "return=minimal");
        http.addHeader("Content-Profile", "public");

        float temp = r.bmeOK ? r.tempC : (r.dhtOK ? r.dhtTempC : 0.0f);
        float hum  = r.bmeOK ? r.humidity : (r.dhtOK ? r.dhtHum : 0.0f);

        ConditionState cs = computeCondition(r);

        // JSON body matching Supabase table columns
        String payload = "{";
        payload += "\"plant_id\":\"peperomia\","; // change plant ID as needed (haworthia, peperomia, and fittonia)
        payload += "\"soil\":" + String(r.soilRaw) + ",";
        payload += "\"light\":" + String(r.lux, 2) + ",";
        payload += "\"temp\":" + String(temp, 2) + ",";
        payload += "\"humidity\":" + String(hum, 2) + ",";
        payload += "\"pump_state\":" + String(pumpState ? 1 : 0) + ",";
        payload += "\"condition\":\"" + cs.label + "\"";
        payload += "}";

        int status = http.POST(payload);

        Serial.print("Supabase POST status: ");
        Serial.println(status);

        if (status > 0) {
            Serial.println("Payload sent:");
            Serial.println(payload);
        } else {
            Serial.println("POST failed!");
            Serial.println(http.errorToString(status));
        }

        http.end();
    }
  }
}