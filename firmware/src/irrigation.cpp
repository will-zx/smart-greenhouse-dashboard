#include "irrigation.h"
#include "config.h"
#include "phase.h"
#include <time.h>

// ==== State machine irigasi (satu pompa: DIST) ====
// Prototipe pakai satu pompa: DIST menarik air langsung dari penampungan utama
// ke zona tanam. Fase FILL (isi ember) dan FERT (dosis pupuk) dibuang;
// PUMP_FILL/PUMP_FERT_* tidak pernah dipanggil firmware.
static IrrigPhase phase = IRRIG_IDLE;
static unsigned long phaseStart = 0;       // mulai fase DIST berjalan
static unsigned long cycleDoseMs = 0;      // durasi pompa siklus yang berjalan
static float         cycleDoseMl = 0.0f;   // dosis siklus yang berjalan
static unsigned long lastCycleStartMs = 0; // awal siklus terakhir (jarak antar siklus)
static bool          hasCycle = false;

// Tracker anggaran harian: volume terkumpul + indeks hari (epoch/86400).
// Volume bertambah saat fase DIST dimulai (dosis yang dikunci untuk siklus itu).
static float         usedMl = 0.0f;
static unsigned long usedDayIndex = 0;
static bool          usedDayInit = false;

// Log penolakan dibatasi 1x per 60 detik per alasan (jangan spam tiap 2 s).
static char          lastRejectKey[16] = "";
static unsigned long lastRejectMs = 0;

// Alasan permintaan terakhir ditahan, dikirim ke dashboard supaya operator
// tahu kenapa air tidak keluar (bukan cuma "tidak merespons"). Kosong = tidak
// ditahan.
static char          holdReason[40] = "";

static void setHoldReason(const char* r) {
    strncpy(holdReason, r, sizeof(holdReason) - 1);
    holdReason[sizeof(holdReason) - 1] = '\0';
}

// Permintaan manual: sakelar operator dinyalakan (turun = override dilepas).
// prevManual dipakai untuk mengenali permintaan BARU; manualRequestUsed
// memastikan satu permintaan manual hanya menghasilkan satu siklus, supaya
// pompa tidak menyala terus-menerus selama override menyala.
static bool          prevManual = false;
static bool          manualRequestUsed = false;

// ==== Tmean: rata-rata suhu sliding (ember 1 menit x 180 ember = 3 jam) ====
// FAO-56 menetapkan Penman-Monteith sebagai metode ETo standar; Blaney-
// Criddle (FAO-24) dikalibrasi untuk periode panjang. Rata-rata 3 jam ini
// PENDEKATAN PRAKTIS, bukan ETo standar.
static float    tmeanBucket[BC_TMEAN_BUCKETS];
static float    tmeanSum = 0.0f;
static int      tmeanValid = 0;             // jumlah ember yang terisi
static unsigned long tmeanBucketIdx = 0;     // indeks ember terakhir

// Blaney-Criddle: ETo = p * (0,46*Tmean + 8), ETc = Kc * ETo.
// Kc diambil dari fase yang sedang berjalan (Tabel 2.1 proposal).
float calculateEtc() {
    float tmean = irrigationTmean();
    if (isnan(tmean)) return 0.0f;
    float eto = constrain(BC_P_FACTOR * (BC_T_COEF * tmean + BC_T_CONST), 0.0f, MAX_ET0);
    return eto * phaseKc();
}

void irrigationFeedTemp(float temp) {
    if (isnan(temp)) return;                 // jangan racuni jendela dengan NaN
    unsigned long bucket = millis() / BC_TMEAN_BUCKET_MS;
    if (bucket != tmeanBucketIdx) {
        // Maju ember: bersihkan ember yang terlewati (bisa banyak kalau
        // device sempat mati/terhenti) supaya jendela tetap "3 jam terakhir".
        unsigned long gap = bucket - tmeanBucketIdx;
        unsigned long steps = (gap > (unsigned long)BC_TMEAN_BUCKETS)
                                ? (unsigned long)BC_TMEAN_BUCKETS : gap;
        for (unsigned long k = 1; k <= steps; k++) {
            unsigned long idx = (tmeanBucketIdx + k) % BC_TMEAN_BUCKETS;
            if (!isnan(tmeanBucket[idx])) {
                tmeanSum -= tmeanBucket[idx];
                tmeanBucket[idx] = NAN;
                tmeanValid--;
            }
        }
        tmeanBucketIdx = bucket;
    }
    unsigned int idx = bucket % BC_TMEAN_BUCKETS;
    if (isnan(tmeanBucket[idx])) tmeanValid++;
    else tmeanSum -= tmeanBucket[idx];
    tmeanBucket[idx] = temp;
    tmeanSum += temp;
}

float irrigationTmean() {
    if (tmeanValid <= 0) return NAN;
    return tmeanSum / (float)tmeanValid;
}

// ==== Dosis dari ETc ====
float irrigationDailyBudgetMl() {
    float etc = calculateEtc();
    if (etc <= 0.0f) return 0.0f;                 // Tmean belum ada -> belum ada anggaran
    return etc * IRRIG_AREA_CM2 * 0.1f;           // mm x cm2 x 0,1 = mL
}

float irrigationDoseMl() {
    if (IRRIG_CYCLES_PER_DAY <= 0) return 0.0f;
    return irrigationDailyBudgetMl() / (float)IRRIG_CYCLES_PER_DAY;
}

unsigned long irrigationDoseMs() {
    float dose = irrigationDoseMl();
    if (dose <= 0.0f || PUMP_DIST_ML_PER_S <= 0.0f) return 0UL;
    return (unsigned long)(dose / PUMP_DIST_ML_PER_S * 1000.0f);
}

unsigned long irrigationMinIntervalMs() {
    if (IRRIG_CYCLES_PER_DAY <= 0) return 0UL;
    return 86400000UL / (unsigned long)IRRIG_CYCLES_PER_DAY;   // 24 jam / siklus per hari
}

// Indeks hari untuk reset anggaran harian: epoch (WIB) supaya reset terjadi
// tengah malam; kalau jam belum sinkron dipakai hari uptime.
static unsigned long currentDayIndex() {
    time_t t = time(nullptr);
    if (t >= 100000) return (unsigned long)(t / 86400);
    return 0x40000000UL + (millis() / 86400000UL);
}

static void budgetRollOver() {
    unsigned long day = currentDayIndex();
    if (!usedDayInit) {
        usedDayInit = true;
        usedDayIndex = day;
        usedMl = 0.0f;
        return;
    }
    if (day != usedDayIndex) {
        usedDayIndex = day;
        usedMl = 0.0f;
        Serial.println("[irigasi] hari berganti: anggaran harian direset");
    }
}

IrrigStatus irrigationStatus() {
    IrrigStatus st;
    st.budgetMl = irrigationDailyBudgetMl();
    st.usedMl = usedMl;
    st.minIntervalMs = irrigationMinIntervalMs();
    st.doseMs = (phase == IRRIG_DIST) ? cycleDoseMs : irrigationDoseMs();
    st.doseMl = (phase == IRRIG_DIST) ? cycleDoseMl : irrigationDoseMl();
    st.holdReason = holdReason;
    return st;
}

// Penggerak pompa: hanya DIST. PUMP_FILL/PUMP_FERT_* tidak pernah disentuh.
static void setDist(bool on) {
    digitalWrite(PUMP_DIST, (on ^ MOSFET_INVERTED) ? HIGH : LOW);
}

// Log penolakan: 1x per 60 detik per alasan.
static void logReject(const char* key, const char* msg) {
    unsigned long now = millis();
    if (strcmp(lastRejectKey, key) == 0 && (now - lastRejectMs) < 60000UL) return;
    strncpy(lastRejectKey, key, sizeof(lastRejectKey) - 1);
    lastRejectKey[sizeof(lastRejectKey) - 1] = '\0';
    lastRejectMs = now;
    Serial.println(msg);
}

bool irrigationRunning() {
    return phase != IRRIG_IDLE;
}

void irrigationAbort() {
    // Sensor tidak valid / darurat: pompa langsung dimatikan. Volume yang
    // sudah dialirkan TIDAK dikembalikan ke tracker (airnya sudah keluar).
    if (phase != IRRIG_IDLE) {
        Serial.println("[irigasi] siklus dibatalkan (abort) - pompa dimatikan");
    }
    setDist(false);
    phase = IRRIG_IDLE;
}

void irrigationUpdate(bool trigger, bool manual) {
    unsigned long now = millis();
    budgetRollOver();

    // Permintaan manual BARU = sakelar operator baru dinyalakan. Override
    // yang dilepas (manual false) membuka permintaan manual berikutnya.
    bool manualNew = manual && !prevManual;
    prevManual = manual;
    if (!manual) manualRequestUsed = false;

    // Syarat memulai siklus: trigger true DAN jeda minimum sudah lewat DAN
    // (terkumpul + dosis <= anggaran harian).
    // Permintaan yang ditahan (jeda/budget) DICoba ulang otomatis selama
    // trigger tetap true -> tidak ada "irigasi mati permanen" seperti dulu.
    // Override manual operator boleh menembus jeda & anggaran (perintah
    // eksplisit), tapi hanya SATU siklus per permintaan manual.
    if (trigger && phase == IRRIG_IDLE) {
        float   budget = irrigationDailyBudgetMl();
        float   dose = irrigationDoseMl();
        unsigned long doseMs = irrigationDoseMs();
        char msg[120];

        bool budgetFull    = (usedMl + dose > budget + 0.5f);
        bool intervalShort = hasCycle && (now - lastCycleStartMs) < irrigationMinIntervalMs();
        bool manualAllow   = manual && manualNew && !manualRequestUsed;

        if (dose <= 0.0f || doseMs == 0UL) {
            setHoldReason("ETc belum tersedia");
            snprintf(msg, sizeof(msg),
                     "[irigasi] ETc belum tersedia, irigasi ditahan");
            logReject("etc", msg);
        } else if (budgetFull && !manualAllow) {
            setHoldReason("anggaran harian habis");
            snprintf(msg, sizeof(msg),
                     "[irigasi] anggaran harian habis: %.0f/%.0f mL", usedMl, budget);
            logReject("budget", msg);
        } else if (intervalShort && !manualAllow) {
            setHoldReason("jeda minimum belum lewat");
            unsigned long sisa = irrigationMinIntervalMs() - (now - lastCycleStartMs);
            snprintf(msg, sizeof(msg),
                     "[irigasi] jeda minimum belum lewat: sisa %.0f menit", sisa / 60000.0f);
            logReject("jeda", msg);
        } else {
            if (manualAllow && (budgetFull || intervalShort)) {
                Serial.println("[irigasi] override manual: siklus jalan di luar "
                               "jeda/anggaran (niat operator)");
            }
            if (manualAllow) manualRequestUsed = true;
            setHoldReason("");
            phase = IRRIG_DIST;
            phaseStart = now;
            cycleDoseMs = doseMs;
            cycleDoseMl = dose;
            lastCycleStartMs = now;
            hasCycle = true;
            usedMl += dose;                      // volume terkumpul hari ini
            Serial.printf("irigasi: dose %.0f mL -> %.0f s | jeda min %.0f jam | "
                          "anggaran harian %.0f mL, terpakai %.0f mL\n",
                          dose, doseMs / 1000.0f,
                          irrigationMinIntervalMs() / 3600000.0f, budget, usedMl);
        }
    }

    if (phase == IRRIG_DIST) {
        setDist(true);
        if (now - phaseStart >= cycleDoseMs) {
            setDist(false);
            phase = IRRIG_IDLE;
            Serial.println("[irigasi] siklus selesai");
        }
    }
}
