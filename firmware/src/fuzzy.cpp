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

// ETc (mm/hari) = kebutuhan air tanaman. Ambang membership mengikuti skala
// Blaney-Criddle yang sudah dikoreksi: p = 0,48 (config.h) dan Kc 0,40-1,10
// (Tabel 2.1 proposal) -> rentang ETc ~2,9 - 13,6 mm/hari.
// Versi lama (0-2-5 & 4-6-12) memang cocok dengan skala ETc lama
// (4,3-6,4 mm/hari); kalau p/Kc diperbaiki tanpa menurunkan ulang ambang,
// etcHigh = 0 di siang hari -> IRIGASI MATI.
static float etcLow(float e)  { return trimf(e, 0.0, 4.0, 8.0); }
static float etcHigh(float e) { return trimf(e, 5.0, 9.0, 18.0); }

static const float MIST_LOW = 0.3, MIST_MED = 0.6, MIST_HIGH = 1.0;
static const float BLOWER_LOW = 0.4, BLOWER_MED = 0.7, BLOWER_HIGH = 1.0;
static const float IRR_LOW = 0.5, IRR_HIGH = 1.0;

// ponytail: rule dalam tabel (bukan banyak if) — index sama di a & b = satu
// anteseden; level 0 hanya menambah bobot (mengencerkan rata-rata = "off").
enum { T_COLD, T_OPT, T_HOT, H_LOW, H_OPT, H_HIGH, S_DRY, S_OPT, S_WET, V_LOW, V_HIGH, E_LOW, E_HIGH };

struct Rule { int a, b; float lvl; };

static const Rule MIST_RULES[] = {
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

static const Rule BLOWER_RULES[] = {
    { T_HOT, H_HIGH, BLOWER_HIGH },
    { T_OPT, H_HIGH, BLOWER_MED  },
    { T_HOT, H_LOW,  BLOWER_LOW  },
    // Pengisi zona RH 45-80: tanpa dua aturan ini duty meloncat 40 % -> 0 % -> 85 %
    // (lihat firmware/tools/out/03_sapuan_duty.png). H_OPT tumpang tindih dengan
    // H_LOW/H_HIGH, jadi transisinya bertahap dan tidak ada RH yang tanpa blower.
    { T_HOT, H_OPT,  BLOWER_MED  },
    { T_OPT, H_OPT,  BLOWER_LOW  },
    { V_LOW, V_LOW,  BLOWER_LOW  },  // udara jenuh → ventilasi turunkan kelembapan
    { T_COLD, T_COLD, 0.0 },
};

// Irigasi: soil = kondisi nyata zona akar, ETc = kebutuhan atmosfer.
// Murni fuzzy — tidak ada bypass hardcoded soil (keputusan pemilik hardware).
static const Rule IRRIG_RULES[] = {
    { S_DRY, E_HIGH, IRR_HIGH },  // tanah kering + evapotranspirasi tinggi → siram penuh
    { S_DRY, E_LOW,  IRR_LOW  },  // tanah kering, ET rendah → siram sedang
    { S_OPT, E_HIGH, IRR_LOW  },  // tanah cukup, ET tinggi → siram sedang
    { S_OPT, E_LOW,  0.0 },
    { S_WET, S_WET,  0.0 },       // tanah basah → jangan siram (anti becek/leaching)
};

// Rata-rata berbobot dari satu tabel rule: min(anteseden) * level, dibagi
// jumlah bobot (level 0 cuma mengencerkan -> "off").
static float evalRules(const Rule* rules, size_t n, const float* mu) {
    float sum = 0, weight = 0;
    for (size_t i = 0; i < n; i++) {
        float w = min(mu[rules[i].a], mu[rules[i].b]);
        sum += w * rules[i].lvl;
        weight += w;
    }
    return (weight > 0) ? sum / weight : 0.0;
}

FuzzyOutput fuzzyDecision(float temp, float humidity, float soilMoisture, float vpd, float etc) {
    float mu[] = {
        tempCold(temp), tempOptimal(temp), tempHot(temp),
        humLow(humidity), humOptimal(humidity), humHigh(humidity),
        soilDry(soilMoisture), soilOptimal(soilMoisture), soilWet(soilMoisture),
        vpdLow(vpd), vpdHigh(vpd),
        etcLow(etc), etcHigh(etc)
    };

    float mistLevel     = evalRules(MIST_RULES,   sizeof(MIST_RULES)   / sizeof(Rule), mu);
    float blowerLevel = evalRules(BLOWER_RULES, sizeof(BLOWER_RULES) / sizeof(Rule), mu);
    float irigasiLevel = evalRules(IRRIG_RULES,  sizeof(IRRIG_RULES)  / sizeof(Rule), mu);

    const char* status;
    if (temp > 34 || temp < 10) status = "Kritis";
    else if (temp > 28 || humidity > 80) status = "Warning";
    else if (temp >= 22 && temp <= 30 && humidity >= 45 && humidity <= 75) status = "Normal";
    else status = "Monitor";

    FuzzyOutput out;
    out.mistOn = mistLevel > 0.1;
    out.blowerOn = blowerLevel > 0.1;
    out.blowerLevel = blowerLevel;
    out.irigasiOn = irigasiLevel > 0.1;
    out.status = status;
    return out;
}