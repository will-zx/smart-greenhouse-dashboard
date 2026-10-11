#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include "config.h"

void initLcd();
void updateLcd(float temp, float humidity, float soilMoisture, float vpd,
               float etc, float tds, const char* status);

#endif
