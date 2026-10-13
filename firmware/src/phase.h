#ifndef PHASE_H
#define PHASE_H

#include <Arduino.h>

#define PHASE_NAME_LEN 24

// Setelan fase pertumbuhan + VPD target. Diubah admin via dashboard (perintah
// Firebase) dan disimpan di NVS agar bertahan setelah reboot / mati listrik.
struct PhaseState {
    bool     active;                     // true bila VPD target sudah pernah di-set
    float    vpdTarget;                  // target VPD (kPa)
    char     name[PHASE_NAME_LEN];       // nama fase (mis. "Vegetatif")
    uint32_t durationSec;                // durasi fase (detik); 0 = tanpa batas
    uint32_t elapsedSec;                 // sudah berjalan sejak durasi di-set (detik)
    uint32_t remainingSec;               // sisa durasi (detik); 0 bila tanpa batas/habis
};

void  phaseInit();                    // muat setelan dari NVS — panggil di setup()
void  phaseSetVpd(float vpd);         // set VPD target (kPa) -> fase aktif
void  phaseSetName(const char* name); // set nama fase
void  phaseSetDuration(uint32_t seconds); // set durasi fase; reset penghitung
void  phaseReset();                   // mulai ulang penghitung durasi dari nol
bool  phaseActive();                  // ada VPD target aktif?
float phaseVpdTarget();               // VPD target saat ini (kPa)
PhaseState phaseGet();                // snapshot lengkap (untuk telemetri/DB)

#endif
