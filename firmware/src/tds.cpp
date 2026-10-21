#include "tds.h"
#include "config.h"

// TDS Meter v1.0 (analog, klon DFRobot SEN0244): output ~0..2.3V.
// Formula pabrikan: TDS = 133.42*V^3 - 255.86*V^2 + 857.39*V, dikali
// kompensasi suhu 1/(1 + 0.02*(T-25)). TDS_CAL_K = faktor kalibrasi probe.
float readTDS(float temperatureC, bool* valid) {
    if (valid) *valid = false;
    if (isnan(temperatureC)) temperatureC = 25.0;  // sensor mati -> tanpa kompensasi
    // Rata-rata beberapa sampel meredam noise ADC (pompa fert mengaduk air).
    float sum = 0;
    const int N = 8;
    for (int i = 0; i < N; i++) {
        sum += analogRead(TDS_PIN);
        delay(10);
    }
    float raw = sum / N;
    // Probe dicabut / tidak terendam -> ADC mengambang (0 atau 4095) atau
    // membaca ~0 V. Nilai seperti itu jangan dipakai: dosis pupuk berbasis
    // angka garbage = tidak pernah jalan atau jalan terus.
    if (raw < TDS_RAW_MIN || raw > TDS_RAW_MAX) return 0.0f;

    float v = raw / TDS_ADC_MAX * TDS_REF_VOLT;
    float comp = 1.0 / (1.0 + 0.02 * (temperatureC - 25.0));
    float ppm = (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * comp * TDS_CAL_K;
    if (isnan(ppm) || ppm < TDS_PPM_MIN || ppm > TDS_PPM_MAX) return 0.0f;

    if (valid) *valid = true;
    return ppm;
}
