#include "filter.h"

void emaInit(EmaFilter& f, float alpha) {
    f.alpha = constrain(alpha, 0.0f, 1.0f);
    f.value = 0.0f;
    f.seeded = false;
}

float emaUpdate(EmaFilter& f, float sample) {
    if (!f.seeded) {
        f.value = sample;      // sampel pertama mengisi filter (hindari ramp dari 0)
        f.seeded = true;
    } else {
        f.value += f.alpha * (sample - f.value);
    }
    return f.value;
}
