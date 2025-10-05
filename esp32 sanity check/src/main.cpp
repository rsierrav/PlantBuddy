#include <Arduino.h>
#include "bme.h"
#include "lcd.h"
#include "relayPump.h"

void setup()
{
  Serial.begin(115200);
  bmeSetup();
  lcdSetup();
  relayPumpSetup();
}

void loop()
{
  float t, rh, p, gas;
  if (bmeReadOnce(t, rh, p, gas))
  {
    // Show sensor data on LCD
    char buf[17];
    snprintf(buf, sizeof(buf), "T:%.1fC RH:%.0f%%", t, rh);
    // Use LCD message logic from lcdTest.cpp
    extern LiquidCrystal_I2C lcd; // Use the same lcd object
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(buf);
    lcd.setCursor(0, 1);
    lcd.print("Plant Buddy!");
    // Control relay based on temperature
    if (t < 30.0)
      relayPumpOn();
    else
      relayPumpOff();
  }
  else
  {
    lcdShowHello();
  }
  delay(2000);
}
