#ifndef FIREBASE_DB_H
#define FIREBASE_DB_H

#include <Arduino.h>

// Push satu log sensor ke /sensor_logs. Mengembalikan true jika HTTP 2xx.
// bodyOut (opsional) diisi JSON yang dikirim — dipakai SD logger utk antrian.
bool pushSensorLog(float temperature, float humidity, float soil, float vpd, float etc,
                   const char* statusSistem, bool manualMode, String* bodyOut = nullptr);
// POST body JSON apa pun ke /sensor_logs (untuk backfill backlog MicroSD).
bool httpPostSensorLogs(const String& body);
void pollCommands();

#endif