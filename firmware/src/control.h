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
//
// Status override per aktuator (dikirim ke RTDB supaya dashboard bisa
// menampilkan sakelar override, bukan status aktuator):
//   -1 = "auto" (ikut fuzzy), 0 = "forced" (dipaksa mati), 1 = "manual" (dipaksa nyala)
struct OverrideState {
    int8_t mist, irig, blower;
};

void controlInit();   // buat mutex — panggil sebelum task dibuat
void controlRelease();// semua aktuator kembali ke kendali fuzzy
void controlSet(uint8_t bit, bool on);   // on = paksa nyala, off = kembali ke fuzzy
// Mode override eksplisit. controlSet() saja tidak cukup untuk dashboard:
// value:false berarti "kembali ke fuzzy", jadi aktuator yang dinyalakan fuzzy
// langsung nyala lagi dan sakelar OFF tidak pernah benar-benar mematikan.
//   -1 = auto (ikut fuzzy), 0 = paksa mati, 1 = paksa nyala.
// Mode 0 dan 1 sama-sama distempel waktu, jadi expireManual() mengembalikan
// aktuator ke fuzzy setelah MANUAL_MAX_MS. Nilai di luar -1..1 dijepit.
void controlForce(uint8_t bit, int8_t mode);
// Terapkan override per aktuator. blowerLevel (opsional) = 1.0/0.0 bila
// blower di-override.
void controlApply(bool* mist, bool* irigasi, bool* blower, float* blowerLevel = nullptr);
bool controlIsManual();
OverrideState controlOverrides();   // status override saat ini (untuk telemetri)

#endif
