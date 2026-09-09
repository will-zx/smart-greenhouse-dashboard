#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <Arduino.h>

void initTelegram();
void pollTelegram(float temp, float humidity, float soilMoisture, float vpd,
                  const char* status, bool mist, bool irigasi, bool blower);
void sendTelegramAlert(const char* message);

#endif