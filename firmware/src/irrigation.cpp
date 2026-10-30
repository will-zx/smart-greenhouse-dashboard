#include "irrigation.h"
#include "config.h"
#include "phase.h"
#include <Preferences.h>
#include <time.h>

// ==== State machine fertigasi (empat pompa) ====
// FILL mengisi ember campuran dari penampungan, FERT menyalakan pompa stok A
// dan B selama durasi tetap, SETTLE mematikan semua pompa supaya larutan
// homogen, CHECK menilai konsentrasi TDS, dan DIST mengalirkan larutan ke
// tanaman. CHECK selalu didahulukan DIST, jadi larutan yang belum diperiksa
// tidak pernah sampai ke tanaman.
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
// Peringatan yang TIDAK menahan air (mis. probe TDS tidak terbaca). Dipisah
// dari holdReason supaya dashboard tidak menulis "siklus ditahan" saat DIST
// tetap berjalan.
static char          warningReason[40] = "";
static Preferences   prefsIrig;
static bool          budgetLoaded = false;

// Siklus terakhir dibatalkan karena TDS di luar rentang. Alasan itu DIPERTAHANKAN
// (tidak ditimpa "jeda minimum belum lewat" 2 detik kemudian) sampai siklus
// berikutnya benar-benar mulai - operator perlu tahu bahwa dosisnya yang salah,
// bukan sekadar jeda waktu.
static bool          tdsBlocked = false;

static void setHoldReason(const char* r) {
    strncpy(holdReason, r, sizeof(holdReason) - 1);
    holdReason[sizeof(holdReason) - 1] = '\0';
}

static void setWarningReason(const char* r) {
    strncpy(warningReason, r, sizeof(warningReason) - 1);
    warningReason[sizeof(warningReason) - 1] = '\0';
}

// Anggaran harian harus bertahan setelah reboot/brownout; kalau hanya di RAM,
// setiap restart mengizinkan satu anggaran penuh lagi.
static void loadBudget() {
    if (budgetLoaded) return;
    budgetLoaded = true;
    prefsIrig.begin("smartgh_irig", false);
    usedMl = prefsIrig.getFloat("used_ml", 0.0f);
    usedDayIndex = prefsIrig.getULong("used_day", 0);
    if (!isfinite(usedMl) || usedMl < 0.0f) usedMl = 0.0f;
}

static void persistBudget() {
    prefsIrig.putFloat("used_ml", usedMl);
    prefsIrig.putULong("used_day", usedDayIndex);
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

// p per bulan dari Tabel FAO-24 (SUN_P_MONTH di config.h). Bulan diambil dari
// jam WIB supaya p mengikuti musim; kalau jam belum sinkron dipakai Januari
// sebagai isi awal (selisihnya kecil, p hanya bergerak 0.266-0.284).
static float pFactorSaatIni() {
    int m = 1;
    time_t t = time(nullptr);
    if (t >= 100000) {
        struct tm lt;
        localtime_r(&t, &lt);
        m = lt.tm_mon + 1;
    }
    if (m < 1 || m > 12) m = 1;
    return SUN_P_MONTH[m - 1];
}

// Blaney-Criddle: ETo = p * (0,46*Tmean + 8), ETc = Kc * ETo.
// Kc diambil dari fase yang sedang berjalan (Tabel 2.1 proposal).
float calculateEtc() {
    float tmean = irrigationTmean();
    if (isnan(tmean)) return 0.0f;
    float eto = constrain(pFactorSaatIni() * (BC_T_COEF * tmean + BC_T_CONST),
                          0.0f, MAX_ET0);
    return eto * phaseKc();
}

void irrigationFeedTemp(float temp) {
    // PENTING: array static terisi 0,0 (bukan NaN), sedangkan sentinel "ember
    // kosong" di sini adalah NaN. Tanpa inisialisasi ini isnan() tidak pernah
    // true -> tmeanValid tidak pernah bertambah -> irrigationTmean() selalu
    // NaN -> ETc 0 -> irigasi tidak pernah jalan (pernah terjadi di device).
    static bool tmeanInit = false;
    if (!tmeanInit) {
        for (int k = 0; k < BC_TMEAN_BUCKETS; k++) tmeanBucket[k] = NAN;
        tmeanInit = true;
    }
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

// Indeks hari untuk reset anggaran harian: epoch WIB (UTC+7) supaya reset
// terjadi tengah malam lokal; kalau jam belum sinkron dipakai hari uptime.
static unsigned long currentDayIndex() {
    time_t t = time(nullptr);
    if (t >= 100000) return (unsigned long)((t + 7 * 3600) / 86400);
    return 0x40000000UL + (millis() / 86400000UL);
}

static void budgetRollOver() {
    loadBudget();
    unsigned long day = currentDayIndex();
    if (!usedDayInit) {
        usedDayInit = true;
        // Hari tersimpan sama = lanjutkan pemakaian; beda/kosong = mulai baru.
        if (usedDayIndex != day) usedMl = 0.0f;
        usedDayIndex = day;
        persistBudget();
        return;
    }
    if (day != usedDayIndex) {
        usedDayIndex = day;
        usedMl = 0.0f;
        persistBudget();
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
    st.warningReason = warningReason;
    return st;
}

// Penggerak empat pompa, digabung dalam satu pemanggilan supaya tidak ada
// keadaan separuh: bila satu pompa menyala, tiga lainnya pasti mati.
static void setPumps(bool fill, bool fertA, bool fertB, bool dist) {
    digitalWrite(PUMP_FILL,   (fill  ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_FERT_A, (fertA ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_FERT_B, (fertB ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(PUMP_DIST,   (dist  ^ MOSFET_INVERTED) ? HIGH : LOW);
}
static void allPumpsOff() { setPumps(false, false, false, false); }

const char* irrigationPhaseName() {
    switch (phase) {
        case IRRIG_IDLE:   return "IDLE";
        case IRRIG_FILL:   return "FILL";
        case IRRIG_FERT:   return "FERT";
        case IRRIG_SETTLE: return "SETTLE";
        case IRRIG_CHECK:  return "CHECK";
        case IRRIG_DIST:   return "DIST";
    }
    return "?";
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
        Serial.println("[irigasi] siklus dibatalkan (abort) - semua pompa dimatikan");
    }
    allPumpsOff();
    phase = IRRIG_IDLE;
}

// Ganti fase + catat waktunya + cetak ke serial. Log perpindahan fase ini yang
// dipakai mengukur IRRIG_FILL_MS / IRRIG_FERT_MS / IRRIG_SETTLE_MS di lapangan.
static void changePhase(IrrigPhase p) {
    if (p == phase) return;
    phase = p;
    phaseStart = millis();
    Serial.printf("irigasi: fase %s\n", irrigationPhaseName());
}

// Masuk fase baru DAN langsung menyalakan pompa fase itu. Pompa dinyalakan di
// sini, bukan di dalam switch, karena waktu diperiksa di awal iterasi: fase
// yang lebih pendek dari periode controlTask (2000 ms, kasus FERT 1126 ms)
// akan menyala lalu mati dalam iterasi yang sama kalau pompanya baru dinyalakan
// setelah pemeriksaan - pompa tidak pernah benar-benar berputar.
static void enterPhase(IrrigPhase p) {
    changePhase(p);
    switch (p) {
        case IRRIG_FILL: setPumps(true,  false, false, false); break;
        case IRRIG_FERT: setPumps(false, true,  true,  false); break;
        case IRRIG_DIST: setPumps(false, false, false, true);  break;
        default:         allPumpsOff(); break;   // SETTLE, CHECK, IDLE
    }
}

void irrigationUpdate(bool trigger, bool manual, float tdsPpm, bool tdsValid) {
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
            // Jangan timpa alasan TDS: itu yang harus dibaca operator.
            if (!tdsBlocked) {
                setHoldReason("jeda minimum belum lewat");
                unsigned long sisa = irrigationMinIntervalMs() - (now - lastCycleStartMs);
                snprintf(msg, sizeof(msg),
                         "[irigasi] jeda minimum belum lewat: sisa %.0f menit", sisa / 60000.0f);
                logReject("jeda", msg);
            }
        } else {
            if (manualAllow && (budgetFull || intervalShort)) {
                Serial.println("[irigasi] override manual: siklus jalan di luar "
                               "jeda/anggaran (niat operator)");
            }
            if (manualAllow) manualRequestUsed = true;
            setHoldReason("");
            setWarningReason("");
            tdsBlocked = false;                  // siklus baru: alasan TDS lama selesai
            cycleDoseMs = doseMs;
            cycleDoseMl = dose;
            lastCycleStartMs = now;
            hasCycle = true;
            usedMl += dose;                      // volume terkumpul hari ini
            persistBudget();
            Serial.printf("irigasi: dose %.0f mL -> %.0f s | jeda min %.0f jam | "
                          "anggaran harian %.0f mL, terpakai %.0f mL\n",
                          dose, doseMs / 1000.0f,
                          irrigationMinIntervalMs() / 3600000.0f, budget, usedMl);
            enterPhase(IRRIG_FILL);              // isi ember dulu, bukan langsung DIST
        }
    }

    switch (phase) {
    // Pola tiap fase: pompa sudah dinyalakan oleh enterPhase() saat fase
    // dimasuki, di sini hanya diperiksa kapan fase berakhir. Jangan menyalakan
    // pompa di dalam case: fase yang lebih pendek dari periode controlTask
    // (FERT 1126 ms vs 2000 ms) akan menyala lalu mati di iterasi yang sama.
    // ---- FILL: penampungan -> ember campuran. Durasi masih perkiraan. ----
    case IRRIG_FILL:
        if (now - phaseStart >= IRRIG_FILL_MS ||
            now - phaseStart >= IRRIG_PHASE_TIMEOUT_MS) {
            enterPhase(IRRIG_FERT);
        }
        break;

    // ---- FERT: stok A dan B -> ember, durasi tetap (lingkaran terbuka). ----
    // Resolusi waktu = periode controlTask 2 s, jadi dosis stok minimal satu
    // iterasi (~2 s x 22,2 mL/s = ~44 mL) walau IRRIG_FERT_MS 1126 ms.
    // Selisih ini yang dikejar lewat fase CHECK/TDS atau pengenceran stok.
    case IRRIG_FERT:
        if (now - phaseStart >= IRRIG_FERT_MS ||
            now - phaseStart >= IRRIG_PHASE_TIMEOUT_MS) {
            enterPhase(IRRIG_SETTLE);
        }
        break;

    // ---- SETTLE: semua pompa mati, larutan homogen sebelum TDS dibaca. ----
    case IRRIG_SETTLE:
        if (now - phaseStart >= IRRIG_SETTLE_MS ||
            now - phaseStart >= IRRIG_PHASE_TIMEOUT_MS) {
            enterPhase(IRRIG_CHECK);
        }
        break;

    // ---- CHECK: nilai TDS. Di luar rentang = distribusi DITAHAN. ----
    // Dua kasus dibedakan sengaja:
    //  - Larutan TERBACA dan jelas di luar rentang (kimia salah): DIST ditahan,
    //    siklus dibatalkan, dan anggaran harian dikembalikan karena air tidak
    //    pernah sampai ke tanaman.
    //  - Probe TIDAK terbaca (tdsValid=false, mis. probe dicabut/tidak stabil):
    //    BUKAN alasan menahan air. Tanpa probe, tanaman tidak akan pernah
    //    disiram, jadi larutan tetap dialirkan dan hanya diberi peringatan.
    case IRRIG_CHECK:
        if (now - phaseStart >= IRRIG_CHECK_MS) {
            char msg[160];
            if (tdsValid && (tdsPpm < TDS_MIN_PPM || tdsPpm > TDS_MAX_PPM)) {
                setHoldReason("TDS di luar rentang, distribusi ditahan");
                setWarningReason("");
                snprintf(msg, sizeof(msg),
                         "[irigasi] TDS %.0f ppm di luar %.0f..%.0f ppm: distribusi "
                         "DITAHAN, siklus dibatalkan (air di ember tidak dialirkan)",
                         tdsPpm, TDS_MIN_PPM, TDS_MAX_PPM);
                logReject("tds_hold", msg);
                tdsBlocked = true;               // alasan ini dipertahankan sampai siklus berikutnya
                // Air tidak jadi dialirkan -> anggaran harian dikembalikan.
                usedMl -= cycleDoseMl;
                if (usedMl < 0.0f) usedMl = 0.0f;
                persistBudget();
                cycleDoseMs = 0;
                cycleDoseMl = 0.0f;
                enterPhase(IRRIG_IDLE);
                // Jeda minimum tetap berlaku supaya tidak terjadi lingkar
                // FILL berulang tiap 2 s (ember terus diisi, tidak ada yang
                // dialirkan). Operator bisa mencoba lagi kapan saja lewat
                // sakelar manual (satu permintaan = satu siklus).
                Serial.println("[irigasi] siklus dibatalkan (TDS). Perbaiki dosis, "
                               "lalu nyalakan sakelar irigasi manual untuk mencoba lagi");
                break;
            }
            if (!tdsValid) {
                setWarningReason("TDS tidak terbaca (air tetap dialirkan)");
                snprintf(msg, sizeof(msg),
                         "[irigasi] TDS tidak terbaca: air tetap didistribusikan");
                logReject("tds", msg);
            } else {
                setWarningReason("");
                Serial.printf("irigasi: TDS %.0f ppm lolos (%.0f..%.0f ppm)\n",
                              tdsPpm, TDS_MIN_PPM, TDS_MAX_PPM);
            }
            enterPhase(IRRIG_DIST);
        }
        break;

    // ---- DIST: ember -> tanaman, berhenti pada dosis yang sudah dihitung. ----
    case IRRIG_DIST:
        if (now - phaseStart >= cycleDoseMs) {
            enterPhase(IRRIG_IDLE);
            Serial.println("[irigasi] siklus selesai");
        }
        break;

    case IRRIG_IDLE:
    default:
        break;
    }
}
