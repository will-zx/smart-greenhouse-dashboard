#ifndef FUZZY_H
#define FUZZY_H

#include <Arduino.h>

struct FuzzyOutput {
    bool mistOn;
    bool blowerOn;
    float blowerLevel;   // 0..1, untuk kecepatan PWM blower (presisi VPD)
    bool irigasiOn;      // keputusan WAKTU irigasi (soil x ETc) -> memicu siklus 4 pompa
    float irigasiLevel;  // 0..1 (cadangan untuk durasi/kuat, belum dipakai)
    const char* status;
};

FuzzyOutput fuzzyDecision(float temp, float humidity, float soilMoisture, float vpd, float etc);

#endif