#include <Arduino.h>

#define SENSORE1 34
// #define SENSORE2 35

// put function declarations here:
int myFunction(int, int);


static int get_soil_moisture_pot1(){
  int moisture_value_1 = analogRead(SENSORE1);
  // int moisture_value_2 = analogRead(SENSORE2);
  Serial.print("Moisture value 1: ");
  Serial.println(moisture_value_1);
  return moisture_value_1;
}

void setup() {
  // put your setup code here, to run once:
  // int result = myFunction(2, 3);
  pinMode(SENSORE1, INPUT);
  // pinMode(SENSORE2, INPUT);
  Serial.begin(115200);  
}

void loop() {
  int i = 0;
  float avg_moisture_value_1 = 0;
  
  while(i < 10){
    avg_moisture_value_1 += get_soil_moisture_pot1();
    delay(1000);
    i++;
  }
  avg_moisture_value_1 = avg_moisture_value_1 / 10;
  Serial.print("Average moisture value 1: ");
  Serial.println(avg_moisture_value_1);
  i=0;
  delay(1000);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}