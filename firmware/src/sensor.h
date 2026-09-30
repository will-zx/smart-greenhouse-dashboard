#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

struct SensorData {
    float temperature;
    float humidity;
    float soilMoisture;
    bool valid;
};

void initSensor();
SensorData readSensors(float soilRaw);
const char* getSensorError();

#endif