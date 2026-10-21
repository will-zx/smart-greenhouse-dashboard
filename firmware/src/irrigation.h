#ifndef IRRIGATION_H
#define IRRIGATION_H

#include <Arduino.h>

// Siklus irigasi 4 pompa: FILL -> FERT -> DIST -> IDLE.
// Dipicu rising-edge `trigger` (dari fuzzy.irigasiOn atau manual dashboard).
enum IrrigPhase { IRRIG_IDLE, IRRIG_FILL, IRRIG_FERT, IRRIG_DIST };

float calculateEtc(float temp);   // Blaney-Criddle: ETc = Kc * p * (0.46*T + 8)
// tdsValid = false bila probe TDS tidak terbaca sah -> dosis pupuk dilewati
// (tidak menebak dari angka garbage), air tetap dialirkan ke tanaman.
void irrigationUpdate(bool trigger, float tdsPpm, bool tdsValid);
IrrigPhase irrigationPhase();      // fase sekarang; dipakai reset filter TDS
bool irrigationRunning();         // ada fase aktif (dipakai status_sistem)
void irrigationAbort();           // manual OFF / darurat

#endif