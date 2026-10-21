#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

struct SensorData {
    float temperature;
    float humidity;
    float soilMoisture;
    bool  valid;        // SHT31 (mikroklimat) terbaca & masuk rentang
    bool  soilValid;    // probe tanah dalam rentang raw yang masuk akal
};

void initSensor();
SensorData readSensors(float soilRaw);
const char* getSensorError();

#endif