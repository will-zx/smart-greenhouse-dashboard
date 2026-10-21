#ifndef IRRIGATION_H
#define IRRIGATION_H

#include <Arduino.h>

// Siklus irigasi 4 pompa: FILL -> FERT -> DIST -> IDLE.
// Dipicu rising-edge `trigger` (dari fuzzy.irigasiOn atau manual dashboard).
enum IrrigPhase { IRRIG_IDLE, IRRIG_FILL, IRRIG_FERT, IRRIG_DIST };

// Kumpulkan sampel suhu untuk Tmean (rata-rata sliding 3 jam).
// Dipanggil tiap iterasi controlTask dengan suhu yang SUDAH tervalidasi.
void irrigationFeedTemp(float temp);

// Tmean = rata-rata suhu sliding BC_TMEAN_BUCKETS menit terakhir (°C).
// NaN bila belum ada sampel sah sama sekali.
float irrigationTmean();

// ETc (mm/hari) = Kc(fase) * p * (0,46*Tmean + 8).
// Pendekatan praktis Blaney-Criddle, BUKAN ETo standar FAO-56.
float calculateEtc();

// tdsValid = false bila probe TDS tidak terbaca sah -> dosis pupuk dilewati
// (tidak menebak dari angka garbage), air tetap dialirkan ke tanaman.
void irrigationUpdate(bool trigger, float tdsPpm, bool tdsValid);
IrrigPhase irrigationPhase();      // fase sekarang; dipakai reset filter TDS
bool irrigationRunning();         // ada fase aktif (dipakai status_sistem)
void irrigationAbort();           // manual OFF / darurat / sensor tidak valid

#endif
