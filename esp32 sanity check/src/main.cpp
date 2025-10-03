// this tests the LiquidCrystal_I2C library with an ESP32

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Try 0x27 first, some modules use 0x3F
#define LCD_ADDR 0x27  

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

void setup() {
  Wire.begin(21, 22); // SDA = 21, SCL = 22
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Plant Buddy");
  lcd.setCursor(0, 1);
  lcd.print("LCD Hello!");
}

void loop() {
  // Rotate messages
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
