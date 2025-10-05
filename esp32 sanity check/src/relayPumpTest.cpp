#include <Arduino.h>
#include "relayPump.h"

#define RELAY_PIN 27

void relayPumpSetup()
{
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH); // relay OFF (active-LOW)
    delay(2000);
}

void relayPumpOn()
{
    digitalWrite(RELAY_PIN, LOW); // relay ON
}

void relayPumpOff()
{
    digitalWrite(RELAY_PIN, HIGH); // relay OFF
}
