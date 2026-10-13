#ifndef CONTROL_H
#define CONTROL_H

#include <Arduino.h>

// ponytail: 3 aktuator = 1 bitmask, bukan 3 bool terpisah. Tambah aktuator
// ke-4 cukup satu bit lagi; kalau prosesnya makin banyak (>8), ganti struct.
enum : uint8_t { C_MIST = 1 << 0, C_IRIG = 1 << 1, C_BLOWER = 1 << 2 };

void controlInit();   // buat mutex — panggil sebelum task dibuat
void controlRelease();
void controlSet(uint8_t bit, bool on);
void controlApply(bool* mist, bool* irigasi, bool* blower);
bool controlIsManual();

#endif