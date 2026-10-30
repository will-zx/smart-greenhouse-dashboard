#ifndef IRRIGATION_H
#define IRRIGATION_H

#include <Arduino.h>

// Siklus fertigasi: IDLE -> FILL -> FERT -> SETTLE -> CHECK -> DIST -> IDLE.
// Urutan ini dikunci: DIST tidak pernah mendahului CHECK, jadi larutan yang
// belum diperiksa konsentrasinya tidak sampai ke tanaman.
// Dipicu `trigger` (keputusan fuzzy ATAU override manual).
enum IrrigPhase { IRRIG_IDLE, IRRIG_FILL, IRRIG_FERT, IRRIG_SETTLE,
                  IRRIG_CHECK, IRRIG_DIST };

// Nama fase ("FILL", "CHECK", ...) untuk log serial dan status_sistem.
const char* irrigationPhaseName();

// Status irigasi untuk telemetri (dashboard) & log.
struct IrrigStatus {
    float         doseMl;          // dosis per siklus (mL)
    unsigned long doseMs;          // durasi pompa siklus ini (ms)
    float         budgetMl;        // anggaran harian (mL)
    float         usedMl;          // volume yang sudah dialirkan hari ini (mL)
    unsigned long minIntervalMs;   // jeda minimum antar siklus (ms)
    const char*   holdReason;      // alasan permintaan ditahan ("" = tidak ditahan)
    const char*   warningReason;   // peringatan non-blocking ("" = tidak ada)
};

// Kumpulkan sampel suhu untuk Tmean (rata-rata sliding 3 jam).
// Dipanggil tiap iterasi controlTask dengan suhu yang SUDAH tervalidasi.
void irrigationFeedTemp(float temp);

// Tmean = rata-rata suhu sliding BC_TMEAN_BUCKETS menit terakhir (°C).
// NaN bila belum ada sampel sah sama sekali.
float irrigationTmean();

// ETc (mm/hari) = Kc(fase) * p(bulan) * (0,46*Tmean + 8), dengan p dari
// Tabel FAO-24 untuk lintang Semarang (SUN_P_MONTH di config.h).
// Pendekatan praktis Blaney-Criddle, BUKAN ETo standar FAO-56.
float calculateEtc();

// Dosis irigasi dari ETc:
//   anggaran_harian_ml = ETc * IRRIG_AREA_CM2 * 0,1   (1 mm x 1 cm2 = 0,1 mL)
//   dosis_ml           = anggaran_harian_ml / IRRIG_CYCLES_PER_DAY
//   durasi_pompa_ms    = dosis_ml / PUMP_DIST_ML_PER_S * 1000
//   jeda_minimum_ms    = 24 jam / IRRIG_CYCLES_PER_DAY
float irrigationDailyBudgetMl();
float irrigationDoseMl();
unsigned long irrigationDoseMs();
unsigned long irrigationMinIntervalMs();
IrrigStatus irrigationStatus();

// Memproses permintaan irigasi. Siklus baru dimulai HANYA bila: trigger true
// DAN jeda minimum sudah lewat DAN (terkumpul + dosis <= anggaran harian).
// Permintaan yang ditahan (mis. jeda minimum belum lewat) dicoba ulang
// otomatis pada iterasi berikutnya selama trigger tetap true.
//
// manual = override operator memaksa irigasi nyala. Perintah eksplisit itu
// boleh menembus jeda minimum & anggaran harian - TAPI hanya SATU siklus per
// permintaan manual (sakelar harus dimatikan lalu dinyalakan lagi). Tanpa
// batas ini, trigger manual yang tetap true akan membuat pompa menyala
// terus-menerus.
//
// tdsPpm/tdsValid dipakai di fase CHECK. Dosis stok A/B berjalan selama
// IRRIG_FERT_MS (lingkaran terbuka, menurut debit pompa terukur), jadi TDS
// hanya MEMVERIFIKASI dugaan itu.
// Kalau ppm TERBACA sah dan di luar TDS_MIN_PPM..TDS_MAX_PPM, DIST DITAHAN:
// siklus dibatalkan, anggaran harian dikembalikan, holdReason diisi.
// Kalau probe TIDAK terbaca (tdsValid=false), air tetap dialirkan dengan
// peringatan - tanpa probe tanaman tidak akan pernah disiram.
void irrigationUpdate(bool trigger, bool manual, float tdsPpm, bool tdsValid);
bool irrigationRunning();         // ada fase aktif (dipakai status_sistem)
void irrigationAbort();           // sensor tidak valid / darurat -> pompa mati

#endif
