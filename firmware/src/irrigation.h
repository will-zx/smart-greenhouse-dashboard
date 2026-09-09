#ifndef IRRIGATION_H
#define IRRIGATION_H

#include <Arduino.h>

struct IrrigationResult {
    float etc;
    bool shouldIrrigate;
};

IrrigationResult calculateIrrigation(float temp, float soilMoisture);

#endif