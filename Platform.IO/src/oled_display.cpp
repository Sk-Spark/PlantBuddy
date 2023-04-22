//OLed Display
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "AzureIoT.h"
#include "oled_display.h"

// #define SCREEN_WIDTH 128 // OLED display width, in pixels
// #define SCREEN_HEIGHT 64 // OLED display height, in pixels
// // Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// // The pins for I2C are defined by the Wire-library.
// // On an arduino UNO:       A4(SDA), A5(SCL)
// // On an arduino MEGA 2560: 20(SDA), 21(SCL)
// // On an arduino LEONARDO:   2(SDA),  3(SCL)
// // On an ESP-32:             D21(SDA),  D22(SCL)
// #define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
// // #define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
// #define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
boolean displayInitialized = false;

// Function Definition
void clearDisplay(){
  display.clearDisplay();
  display.display();
}

void setupDisplay(){
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if ( !displayInitialized && !display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)){
    Serial.println(F("SSD1306 allocation failed"));    
  }
  else{
    displayInitialized = true;
    clearDisplay();
  }
}

void displayMsg(String msg, int textSize){
  display.clearDisplay();

  display.setTextSize(textSize);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.cp437(true);                 // Use full 256 char 'Code Page 437' font
  display.println(msg);
  display.display();
}

void displayToLed(int temp, int soil_moisture_percent){
  display.clearDisplay();

  display.setTextSize(2);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.cp437(true);                 // Use full 256 char 'Code Page 437' font
 

  String displayText = "Moist: ";
  // Not all the characters will fit on the display. This is normal.
  // Library will draw what it can and the rest will be clipped.
  for (int i = 0; i < displayText.length() ; i++)
  {
    if (i == '\n')
      display.write(' ');
    else
      display.write(displayText[i]);
  }
  display.print(soil_moisture_percent);
  display.println("%\n");

  
  displayText = "Temp: ";
  // Not all the characters will fit on the display. This is normal.
  // Library will draw what it can and the rest will be clipped.
  for (int i = 0; i < displayText.length() ; i++)
  {
    if (i == '\n')
      display.write(' ');
    else
      display.write(displayText[i]);
  }
   display.print(temp);
  display.setTextSize(1);              // Normal 1:1 pixel scale
  display.write(167);
  display.setTextSize(2);              // Normal 1:1 pixel scale
  display.println("C");

  display.display();
  // delay(2000);
}

void displayToLed(int soil_moisture_sensor_val, int soil_moisture_percent, int temp)
{
  display.clearDisplay();

  display.setTextSize(2);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.cp437(true);                 // Use full 256 char 'Code Page 437' font

  String displayText = "Val: ";

  // Not all the characters will fit on the display. This is normal.
  // Library will draw what it can and the rest will be clipped.
  for (int i = 0; i < displayText.length() ; i++)
  {
    if (i == '\n')
      display.write(' ');
    else
      display.write(displayText[i]);
  }
  display.println(soil_moisture_sensor_val);

  displayText = "in(%):";
  // Not all the characters will fit on the display. This is normal.
  // Library will draw what it can and the rest will be clipped.
  for (int i = 0; i < displayText.length() ; i++)
  {
    if (i == '\n')
      display.write(' ');
    else
      display.write(displayText[i]);
  }
  display.print(soil_moisture_percent);
  display.println('%');

  
  displayText = "Temp:";
  // Not all the characters will fit on the display. This is normal.
  // Library will draw what it can and the rest will be clipped.
  for (int i = 0; i < displayText.length() ; i++)
  {
    if (i == '\n')
      display.write(' ');
    else
      display.write(displayText[i]);
  }
   display.print(temp);
  display.setTextSize(1);              // Normal 1:1 pixel scale
  display.write(167);
  display.setTextSize(2);              // Normal 1:1 pixel scale
  display.println("C");

  display.display();
  // delay(2000);
}
