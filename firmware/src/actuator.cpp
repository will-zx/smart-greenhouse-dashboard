#include "actuator.h"

// Modul MOSFET 15A/400W. Umumnya aktif-HIGH (HIGH = ON); kalau modul Anda
// aktif-LOW, cukup set MOSFET_INVERTED 1 di config.h.
static const int BLOWER_CH = 0;

void setActuators(bool mist, bool irigasi, bool blower, float blowerLevel) {
    digitalWrite(MOSFET_MIST,  (mist ^ MOSFET_INVERTED) ? HIGH : LOW);
    digitalWrite(MOSFET_IRRIG, (irigasi ^ MOSFET_INVERTED) ? HIGH : LOW);
    // 2 kipas 12x12cm paralel; kecepatan proporsional blowerLevel (0..1) dari fuzzy.
    uint8_t duty = blower ? (uint8_t)(blowerLevel * ((1 << BLOWER_RES) - 1)) : 0;
    ledcWrite(BLOWER_CH, duty);
}

void initActuators() {
    pinMode(MOSFET_MIST, OUTPUT);
    pinMode(MOSFET_IRRIG, OUTPUT);
    ledcSetup(BLOWER_CH, BLOWER_FREQ, BLOWER_RES);  // 25 kHz: senyap untuk kipas
    ledcAttachPin(BLOWER_PIN, BLOWER_CH);
    setActuators(false, false, false, 0.0);
}