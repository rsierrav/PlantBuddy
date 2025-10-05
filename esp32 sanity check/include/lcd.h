#ifndef LCD_H
#define LCD_H

#include <LiquidCrystal_I2C.h>
#define LCD_ADDR 0x27
extern LiquidCrystal_I2C lcd;

void lcdSetup();
void lcdShowHello();
void lcdRotateMessages();

#endif
