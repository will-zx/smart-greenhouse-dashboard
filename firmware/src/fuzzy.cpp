#include "fuzzy.h"

static float trimf(float x, float a, float b, float c) {
    if (x <= a || x >= c) return 0.0;
    if (x <= b) return (x - a) / (b - a);
    return (c - x) / (c - b);
}

static float tempCold(float t)    { return trimf(t, 10, 18, 25); }
static float tempOptimal(float t) { return trimf(t, 22, 27, 32); }
static float tempHot(float t)     { return trimf(t, 28, 34, 42); }

static float humLow(float h)     { return trimf(h, 0, 30, 55); }
static float humOptimal(float h) { return trimf(h, 45, 65, 80); }
static float humHigh(float h)    { return trimf(h, 70, 85, 100); }

static float soilDry(float s)     { return trimf(s, 0, 15, 35); }
static float soilOptimal(float s) { return trimf(s, 30, 50, 70); }
static float soilWet(float s)     { return trimf(s, 60, 80, 100); }

// Cabai (sayuran terlindung): VPD optimal 0.5-1.5 kPa, stres > 1.5 kPa
// (Yu et al. 2023, "Systemic effects of the VPD...", Vegetable Research 3:20).
static float vpdLow(float v)  { return trimf(v, 0.0, 0.2, 0.5); }
static float vpdHigh(float v) { return trimf(v, 1.3, 1.6, 2.0); }

static const float MIST_LOW = 0.3, MIST_MED = 0.6, MIST_HIGH = 1.0;
static const float BLOWER_LOW = 0.4, BLOWER_MED = 0.7, BLOWER_HIGH = 1.0;

// ponytail: rule dalam tabel (bukan banyak if) — index sama di a & b = satu
// anteseden; level 0 hanya menambah bobot (mengencerkan rata-rata = "off").
enum { T_COLD, T_OPT, T_HOT, H_LOW, H_OPT, H_HIGH, S_DRY, S_OPT, S_WET, V_LOW, V_HIGH };

static const struct { int a, b; float lvl; } MIST_RULES[] = {
    { T_HOT, S_DRY, MIST_HIGH },
    { T_HOT, S_OPT, MIST_MED  },
    { T_HOT, S_WET, MIST_LOW  },
    { T_HOT, V_HIGH, MIST_HIGH },    // udara kering (VPD stres) + panas → mist penuh
    { T_OPT, S_DRY, MIST_MED  },
    { T_OPT, V_HIGH, MIST_MED  },
    { T_COLD, T_COLD, 0.0 },         // dingin → off
    { T_OPT, S_WET, 0.0 },           // tanah basah → off
    { V_LOW, V_LOW, 0.0 },           // udara sudah jenuh → off (anti jamur daun)
};

static const struct { int a, b; float lvl; } BLOWER_RULES[] = {
    { T_HOT, H_HIGH, BLOWER_HIGH },
    { T_OPT, H_HIGH, BLOWER_MED  },
    { T_HOT, H_LOW,  BLOWER_LOW  },
    { V_LOW, V_LOW,  BLOWER_LOW  },  // udara jenuh → ventilasi turunkan kelembapan
    { T_COLD, T_COLD, 0.0 },
};

FuzzyOutput fuzzyDecision(float temp, float humidity, float soilMoisture, float vpd) {
    float mu[] = {
        tempCold(temp), tempOptimal(temp), tempHot(temp),
        humLow(humidity), humOptimal(humidity), humHigh(humidity),
        soilDry(soilMoisture), soilOptimal(soilMoisture), soilWet(soilMoisture),
        vpdLow(vpd), vpdHigh(vpd)
    };

    float mistSum = 0, mistWeight = 0;
    for (const auto& r : MIST_RULES) {
        float w = min(mu[r.a], mu[r.b]);
        mistSum += w * r.lvl; mistWeight += w;
    }

    float blowerSum = 0, blowerWeight = 0;
    for (const auto& r : BLOWER_RULES) {
        float w = min(mu[r.a], mu[r.b]);
        blowerSum += w * r.lvl; blowerWeight += w;
    }

    float mistLevel = (mistWeight > 0) ? mistSum / mistWeight : 0.0;
    float blowerLevel = (blowerWeight > 0) ? blowerSum / blowerWeight : 0.0;

    const char* status;
    if (temp > 34 || temp < 10) status = "Kritis";
    else if (temp > 28 || humidity > 80) status = "Warning";
    else if (temp >= 22 && temp <= 30 && humidity >= 45 && humidity <= 75) status = "Normal";
    else status = "Monitor";

    FuzzyOutput out;
    out.mistOn = mistLevel > 0.1;
    out.blowerOn = blowerLevel > 0.1;
    out.status = status;
    return out;
}