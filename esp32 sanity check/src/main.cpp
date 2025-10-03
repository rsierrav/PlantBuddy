#include <Arduino.h>

void setup() {
  Serial.begin(115200);   // start serial monitor
  pinMode(2, OUTPUT);     // onboard LED is on GPIO 2
}

void loop() {
  digitalWrite(2, HIGH);
  Serial.println("LED ON");
  delay(1000);
  digitalWrite(2, LOW);
  Serial.println("LED OFF");
  delay(1000);
}
