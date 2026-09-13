#ifndef FIREBASE_DB_H
#define FIREBASE_DB_H

#include <Arduino.h>

void pushSensorLog(float temperature, float humidity, float soil, float vpd, float etc,
                   const char* statusSistem, bool manualMode);
void pollCommands();

#endif