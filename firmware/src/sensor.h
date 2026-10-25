#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

struct SensorData {
    float temperature;
    float humidity;
    float soilMoisture;            // rata-rata probe yang valid (%)
    float soilPct[SOIL_COUNT];     // per probe (%), NAN bila probe tidak masuk akal
    float soilRaw[SOIL_COUNT];     // ADC mentah per probe (untuk kalibrasi)
    bool  valid;        // SHT31 (mikroklimat) terbaca & masuk rentang
    bool  soilValid;    // minimal satu probe tanah dalam rentang raw yang masuk akal
};

void initSensor();
SensorData readSensors();
const char* getSensorError();

#endif
