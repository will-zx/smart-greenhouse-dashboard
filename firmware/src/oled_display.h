#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Arduino.h>
#include "config.h"

void initOled();
void updateOled(float temp, float humidity, float soilMoisture, float vpd,
                const char* status);

#endif