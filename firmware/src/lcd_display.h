#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include "config.h"

void initLcd();
void updateLcd(float temp, float humidity, float soilMoisture, float vpd,
               float etc, float tds);

// Baris 4 (status) milik task scroll: controlTask cuma menyerahkan teksnya,
// supaya marquee bisa bergerak lebih cepat dari periode kontrol 2 s.
void lcdScrollStart();
void lcdScrollSetStatus(const char* status);

#endif
