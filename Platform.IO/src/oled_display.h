#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

//OLed Display
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "AzureIoT.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library.
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL)
// On an ESP-32:             D21(SDA),  D22(SCL)
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
// #define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
// extern Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
extern Adafruit_SSD1306 display;
extern boolean displayInitialized;

//Function Declaration
void clearDisplay();
void setupDisplay();
void displayMsg(String msg, int textSize = 2);
void displayToLed(int temp, int soil_moisture_percent);
void displayToLed(int soil_moisture_sensor_val, int soil_moisture_percent, int temp);

#endif // OLED_DISPLAY_H