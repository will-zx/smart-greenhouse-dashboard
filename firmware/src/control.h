#ifndef CONTROL_H
#define CONTROL_H

#include <Arduino.h>

// ponytail: 3 aktuator = 1 bitmask, bukan 3 bool terpisah. Tambah aktuator
// ke-4 cukup satu bit lagi; kalau prosesnya makin banyak (>8), ganti struct.
enum : uint8_t { C_MIST = 1 << 0, C_IRIG = 1 << 1, C_BLOWER = 1 << 2 };

// Override manual TRIS-TATA per aktuator:
//   -1 = ikut fuzzy (default), 0 = paksa mati, 1 = paksa nyala.
// Manual hanya berlaku untuk aktuator yang benar-benar di-override. Dulu
// override memakai satu bitmask buat semua aktuator, jadi mematikan satu
// sakelar mematikan SEMUA aktuator dan tidak ada jalan kembali selain Auto.
void controlInit();   // buat mutex — panggil sebelum task dibuat
void controlRelease();// semua aktuator kembali ke kendali fuzzy
void controlSet(uint8_t bit, bool on);   // on = paksa nyala, off = kembali ke fuzzy
// Terapkan override per aktuator. blowerLevel (opsional) = 1.0/0.0 bila
// blower di-override.
void controlApply(bool* mist, bool* irigasi, bool* blower, float* blowerLevel = nullptr);
bool controlIsManual();

#endif
