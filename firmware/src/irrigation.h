#ifndef IRRIGATION_H
#define IRRIGATION_H

#include <Arduino.h>

// Siklus irigasi: IDLE -> DIST -> IDLE (satu pompa DIST saja; fertigasi
// ditunda). Dipicu `trigger` (keputusan fuzzy ATAU override manual).
enum IrrigPhase { IRRIG_IDLE, IRRIG_DIST };

// Status irigasi untuk telemetri (dashboard) & log.
struct IrrigStatus {
    float         doseMl;          // dosis per siklus (mL)
    unsigned long doseMs;          // durasi pompa siklus ini (ms)
    float         budgetMl;        // anggaran harian (mL)
    float         usedMl;          // volume yang sudah dialirkan hari ini (mL)
    unsigned long minIntervalMs;   // jeda minimum antar siklus (ms)
};

// Kumpulkan sampel suhu untuk Tmean (rata-rata sliding 3 jam).
// Dipanggil tiap iterasi controlTask dengan suhu yang SUDAH tervalidasi.
void irrigationFeedTemp(float temp);

// Tmean = rata-rata suhu sliding BC_TMEAN_BUCKETS menit terakhir (°C).
// NaN bila belum ada sampel sah sama sekali.
float irrigationTmean();

// ETc (mm/hari) = Kc(fase) * p * (0,46*Tmean + 8).
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
void irrigationUpdate(bool trigger);
bool irrigationRunning();         // ada fase aktif (dipakai status_sistem)
void irrigationAbort();           // sensor tidak valid / darurat -> pompa mati

#endif
