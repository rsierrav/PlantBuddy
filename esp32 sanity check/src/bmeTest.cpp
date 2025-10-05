#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include "bme.h"

// SPI pins for BME680
#define BME_CS 5
#define BME_MOSI 23
#define BME_MISO 19
#define BME_SCK 18

static Adafruit_BME680 bme(BME_CS, BME_MOSI, BME_MISO, BME_SCK);
static bool bme_ok = false;

bool bmeSetup()
{
    if (!bme.begin())
    {
        Serial.println("BME680 (SPI) not found — check wiring.");
        bme_ok = false;
        return false;
    }
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320°C for 150 ms
    bme_ok = true;
    return true;
}

bool bmeReadOnce(float &tC, float &rh, float &hPa, float &gas_kohm)
{
    if (!bme_ok)
        return false;
    if (!bme.performReading())
        return false;
    tC = bme.temperature;
    rh = bme.humidity;
    hPa = bme.pressure / 100.0f;
    gas_kohm = bme.gas_resistance / 1000.0f;
    return true;
}
