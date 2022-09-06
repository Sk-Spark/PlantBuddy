/*
# the approximate moisture levels for the sensor reading
# 0 to 300 in water
# 300 to 700 humid soil
# 700 to 950 dry soil
*/

#include <Arduino.h>

// #define ledPin 33
#define ledPin LED_BUILTIN
#define sensorPin 36

int trigger = 700; // set the level

void setup()
{
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW); // turn off LED
   pinMode(LED_BUILTIN, OUTPUT);
   digitalWrite(LED_BUILTIN,LOW);
}

void loop()
{
  Serial.print("Moisture Sensor Value:");
  Serial.println(analogRead(sensorPin)); // read the value from the sensor
  if (analogRead(sensorPin) >= trigger)
  {
    digitalWrite(ledPin, HIGH); // turn on the LED
  }
  else
  {
    digitalWrite(ledPin, LOW); // turn off LED
  }
  delay(1000);
  // digitalWrite(LED_BUILTIN, HIGH);
  // delay(500);
  // digitalWrite(LED_BUILTIN, LOW);
  // Serial.println("");
}