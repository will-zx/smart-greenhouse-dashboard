#include "irrigation.h"
#include "config.h"

// Blaney-Criddle: ETc = Kc * p * (0.46*T + 8), p = fraksi jam siang tahunan.
// p ≈ PEAK_HOURS/12 (ekuator ~0.27; 4/12=0.33 over-estimasi ±20%, sengaja).
// Kc cabai: 0.4 awal → 0.75 vegetatif → 1.1 bunga/buah (tepat fase vegetatif).
// Referensi: "Penetapan Kebutuhan Air Tanaman Cabai Merah" (kc 0.4/0.75/1.1/1.0/0.9),
// "Modified Blaney-Criddle..." (ETo = p x (0.46 Tmean + 8)).
IrrigationResult calculateIrrigation(float temp, float soilMoisture) {
    float etc = constrain((PEAK_HOURS / 12.0) * (0.46 * temp + 8.13), 0.0, MAX_ET0) * KC_CHILI;
    IrrigationResult result;
    result.etc = etc;
    result.shouldIrrigate = (soilMoisture < 40.0) || (etc > 6.0);
    return result;
}