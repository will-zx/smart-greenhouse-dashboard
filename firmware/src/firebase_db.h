#ifndef FIREBASE_DB_H
#define FIREBASE_DB_H

#include <Arduino.h>
#include "phase.h"
#include "control.h"

// Push satu log sensor ke /sensor_logs. Mengembalikan true jika HTTP 2xx.
// bodyOut (opsional) diisi JSON yang dikirim — dipakai SD logger utk antrian.
// phase: setelan fase + VPD target + Kc aktif saat ini (ikut dicatat ke RTDB).
// ov: status override manual per aktuator (mist_ov/irig_ov/blower_ov).
// tmean: suhu rata-rata sliding yang dipakai menghitung ETc (approach label).
bool pushSensorLog(float temperature, float humidity, float soil, float vpd, float etc, float tds,
                   float blowerLevel, bool misting, bool irigasi,
                   const char* statusSistem, bool manualMode, const PhaseState& phase,
                   const OverrideState& ov, float tmean, String* bodyOut = nullptr);
// POST body JSON apa pun ke /sensor_logs (untuk backfill backlog MicroSD).
bool httpPostSensorLogs(const String& body);
void pollCommands();

#endif
