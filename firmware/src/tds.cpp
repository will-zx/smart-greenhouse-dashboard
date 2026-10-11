#include "tds.h"
#include "config.h"

// TDS Meter v1.0 (analog, klon DFRobot SEN0244): output ~0..2.3V.
// Formula pabrikan: TDS = 133.42*V^3 - 255.86*V^2 + 857.39*V, dikali
// kompensasi suhu 1/(1 + 0.02*(T-25)). TDS_CAL_K = faktor kalibrasi probe.
float readTDS(float temperatureC) {
    if (isnan(temperatureC)) temperatureC = 25.0;  // sensor mati -> tanpa kompensasi
    // Rata-rata beberapa sampel meredam noise ADC (pompa fert mengaduk air).
    float sum = 0;
    const int N = 8;
    for (int i = 0; i < N; i++) {
        sum += analogRead(TDS_PIN);
        delay(10);
    }
    float v = (sum / N) / TDS_ADC_MAX * TDS_REF_VOLT;
    float comp = 1.0 / (1.0 + 0.02 * (temperatureC - 25.0));
    return (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * comp * TDS_CAL_K;
}