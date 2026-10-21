#include "irrigation.h"
#include "config.h"
#include "phase.h"

static IrrigPhase phase = IRRIG_IDLE;
static unsigned long phaseStart = 0;

// Status "permintaan irigasi" terakhir. Edge-detect: siklus baru hanya
// mulai saat permintaan naik dari FALSE ke TRUE.
// PENTING: variabel ini TIDAK boleh static di dalam irrigationUpdate(),
// karena irrigationAbort() (dipanggil dari luar, mis. saat sensor tidak
// valid) tidak pernah melewati fungsi itu -> prevTrigger lama bertahan dan
// permintaan yang masih TRUE setelah pemulihan TIDAK bisa memicu siklus
// baru (irigasi mati permanen).
static bool prevTrigger = false;
// TRUE bila permintaan masih aktif saat siklus dibatalkan -> saat sensor
// pulih, siklus langsung jalan lagi (log "dipulihkan").
static bool resumePending = false;

// Jumlah sampel TDS invalid berturut-turut di fase FERT.
static int fertInvalidSamples = 0;

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

static void setPumps(bool fill, bool fertA, bool fertB, bool dist) {
    digitalWrite(PUMP_FILL,   (fill   ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_FERT_A, (fertA  ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_FERT_B, (fertB  ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_DIST,   (dist   ^ MOSFET_INVERTED) ? HIGH : LOW);
}

static void nextPhase(IrrigPhase p) {
    phase = p;
    phaseStart = millis();
    fertInvalidSamples = 0;
    setPumps(false, false, false, false);
}

IrrigPhase irrigationPhase() {
    return phase;
}

bool irrigationRunning() {
    return phase != IRRIG_IDLE;
}

void irrigationAbort() {
    // Reset status "permintaan aktif" supaya permintaan yang masih TRUE
    // setelah pemulihan sensor bisa memulai siklus baru (lihat prevTrigger).
    if (prevTrigger) resumePending = true;
    prevTrigger = false;
    if (phase != IRRIG_IDLE) {
        Serial.println("[irigasi] siklus dibatalkan (abort) - semua pompa mati");
    }
    nextPhase(IRRIG_IDLE);
}

void irrigationUpdate(bool trigger, float tdsPpm, bool tdsValid) {
    // Edge-detect: 0->1 memulai siklus; trigger yang terus tinggi tidak
    // memicu siklus kedua (satu permintaan = satu siklus).
    if (trigger && !prevTrigger && phase == IRRIG_IDLE) {
        if (resumePending) {
            Serial.println("[irigasi] siklus DIPULIHKAN (permintaan masih aktif sejak gangguan)");
            resumePending = false;
        } else {
            Serial.println("[irigasi] siklus dimulai (FILL -> FERT -> DIST)");
        }
        nextPhase(IRRIG_FILL);
    }
    prevTrigger = trigger;
    // Permintaan sudah selesai -> tidak ada yang perlu "dipulihkan" lagi.
    if (!trigger) resumePending = false;

    unsigned long now = millis();
    switch (phase) {
    case IRRIG_FILL:
        setPumps(true, false, false, false);   // 12V: penampungan -> ember campuran
        if (now - phaseStart >= IRRIG_FILL_MS) nextPhase(IRRIG_FERT);
        break;

    case IRRIG_FERT: {
        // Probe TDS tidak terbaca sah -> dosis MALAH dilewati (fail-safe):
        // menebak konsentrasi dari angka garbage bisa membuat over- atau
        // underpupuk. TAPI satu sampel noisey (pompa fert berdenyut) tidak
        // boleh menggagalkan pemupukan -> tunggu IRRIG_FERT_INVALID_LIMIT
        // sampel invalid berturut-turut baru batalkan fase.
        if (!tdsValid) {
            if (++fertInvalidSamples >= IRRIG_FERT_INVALID_LIMIT) {
                Serial.printf("[irigasi] TDS tidak valid %d sampel berturut - dosis dilewati, lanjut DIST\n",
                              fertInvalidSamples);
                nextPhase(IRRIG_DIST);
                break;
            }
            // Masih dalam toleransi: pompa tetap berdenyut (agitasi), tapi
            // konsentrasi TIDAK dihitung dari angka tidak valid.
        } else {
            fertInvalidSamples = 0;
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
        // Gate konsentrasi hanya boleh memakai angka TDS yang sah.
        if (tdsValid && !fertOn && tdsPpm >= TDS_TARGET_PPM - IRRIG_TDS_HYSTERESIS) {
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
