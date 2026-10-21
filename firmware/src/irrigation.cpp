#include "irrigation.h"
#include "config.h"

static IrrigPhase phase = IRRIG_IDLE;
static unsigned long phaseStart = 0;

// Blaney-Criddle: ETc = Kc * p * (0.46*T + 8), p = fraksi jam siang tahunan.
// p ≈ PEAK_HOURS/12 (ekuator ~0.27; 4/12=0.33 over-estimasi ±20%, sengaja).
// Kc cabai: 0.4 awal → 0.75 vegetatif → 1.1 bunga/buah (tepat fase vegetatif).
// Referensi: "Penetapan Kebutuhan Air Tanaman Cabai Merah" (kc 0.4/0.75/1.1/1.0/0.9),
// "Modified Blaney-Criddle..." (ETo = p x (0.46 Tmean + 8)).
float calculateEtc(float temp) {
    return constrain((PEAK_HOURS / 12.0) * (0.46 * temp + 8.13), 0.0, MAX_ET0) * KC_CHILI;
}

static void setPumps(bool fill, bool fertA, bool fertB, bool dist) {
    digitalWrite(PUMP_FILL,   (fill   ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_FERT_A, (fertA  ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_FERT_B, (fertB  ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_DIST,   (dist   ^ MOSFET_INVERTED) ? HIGH : LOW);
}

static void nextPhase(IrrigPhase p) {
    phase = p;
    phaseStart = millis();
    setPumps(false, false, false, false);
}

IrrigPhase irrigationPhase() {
    return phase;
}

bool irrigationRunning() {
    return phase != IRRIG_IDLE;
}

void irrigationAbort() {
    nextPhase(IRRIG_IDLE);
}

void irrigationUpdate(bool trigger, float tdsPpm, bool tdsValid) {
    // Edge-detect: 0->1 memulai siklus; trigger yang terus tinggi tidak
    // memicu siklus kedua (sekali per permintaan).
    static bool prevTrigger = false;
    if (trigger && !prevTrigger && phase == IRRIG_IDLE) nextPhase(IRRIG_FILL);
    prevTrigger = trigger;

    unsigned long now = millis();
    switch (phase) {
    case IRRIG_FILL:
        setPumps(true, false, false, false);   // 12V: penampungan -> ember campuran
        if (now - phaseStart >= IRRIG_FILL_MS) nextPhase(IRRIG_FERT);
        break;

    case IRRIG_FERT: {
        // Probe TDS tidak terbaca sah -> dosis MALAH dilewati (fail-safe):
        // menebak konsentrasi dari angka garbage bisa membuat over- atau
        // underpupuk. Air tetap dialirkan ke tanaman (DIST).
        if (!tdsValid) {
            Serial.println("[irigasi] TDS tidak valid - dosis pupuk dilewati, lanjut DIST");
            nextPhase(IRRIG_DIST);
            break;
        }
        // Pulse: pompa fert nyala 2s (mengaduk larutan) -> jeda 1s (sampling TDS
        // saat air masih bergerak dari pulse terakhir, pembacaan lebih stabil).
        unsigned long t = now - phaseStart;
        bool fertOn = (t % (IRRIG_FERT_PULSE_MS + IRRIG_FERT_IDLE_MS)) < IRRIG_FERT_PULSE_MS;
        setPumps(false, fertOn, fertOn, false);   // 2x 6V: pupuk A & B -> ember
        if (t >= IRRIG_FERT_TIMEOUT_MS) {
            Serial.printf("[irigasi] FERT timeout, TDS tetap %.0f ppm - lanjut DIST\n", tdsPpm);
            nextPhase(IRRIG_DIST);
            break;
        }
        if (!fertOn && tdsPpm >= TDS_TARGET_PPM - IRRIG_TDS_HYSTERESIS) {
            Serial.printf("[irigasi] TDS target tercapai: %.0f ppm\n", tdsPpm);
            nextPhase(IRRIG_DIST);
        }
        break;
    }

    case IRRIG_DIST:
        setPumps(false, false, false, true);   // 12V: ember campuran -> tanaman
        if (now - phaseStart >= IRRIG_DIST_MS) {
            Serial.println("[irigasi] siklus selesai");
            nextPhase(IRRIG_IDLE);
        }
        break;

    default:
        break;
    }
}