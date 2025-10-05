#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "lcd.h"

// LCD config

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

void lcdSetup()
{
  lcd.init();
  lcd.backlight();
}

void lcdShowHello()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Plant Buddy");
  lcd.setCursor(0, 1);
  lcd.print("LCD Hello!");
}

void lcdRotateMessages()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hello ESP32!");
  delay(1000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Rosa and Es's ");
  lcd.setCursor(0, 1);
  lcd.print("Project");
  delay(1000);
}
