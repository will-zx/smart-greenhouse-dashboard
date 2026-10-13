#ifndef FILTER_H
#define FILTER_H

#include <Arduino.h>

// Exponential Moving Average (EMA) — pelunakan sinyal sensor.
// y[n] = alpha*x[n] + (1-alpha)*y[n-1]; alpha kecil => lebih halus, lebih lambat.
struct EmaFilter {
    float alpha;   // 0..1; bobot sampel baru
    float value;   // nilai terfilter saat ini
    bool  seeded;  // false sampai sampel pertama masuk (hindari ramp dari 0)
};

void  emaInit(EmaFilter& f, float alpha);
float emaUpdate(EmaFilter& f, float sample);

#endif
