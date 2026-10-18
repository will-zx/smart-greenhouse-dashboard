#include "actuator.h"

// Modul XY-MOS 400W. Umumnya aktif-HIGH (HIGH = ON); kalau modul Anda
// aktif-LOW, cukup set MOSFET_INVERTED 1 di config.h.
static const int BLOWER_CH = 0;

void setActuators(bool mist, bool blower, float blowerLevel) {
    digitalWrite(MOSFET_MIST, (mist ^ MOSFET_INVERTED) ? HIGH : LOW);
    // 2 kipas 12x12cm paralel; kecepatan proporsional blowerLevel (0..1) dari fuzzy.
    // Level di bawah duty minimum kipas dibulatkan naik: tanpa ini perintah
    // 0 < level < BLOWER_MIN_DUTY menyalakan kipas yang tidak berputar.
    static bool reported = false;
    if (blower && blowerLevel > 0.0f && blowerLevel < BLOWER_MIN_DUTY) {
        if (!reported) {
            Serial.printf("[act] blower duty %.0f%% -> %.0f%% (min kipas)\n",
                          blowerLevel * 100, BLOWER_MIN_DUTY * 100);
            reported = true;
        }
        blowerLevel = BLOWER_MIN_DUTY;
    }
    uint8_t duty = blower ? (uint8_t)(blowerLevel * ((1 << BLOWER_RES) - 1)) : 0;
    ledcWrite(BLOWER_CH, duty);
}

void initActuators() {
    pinMode(MOSFET_MIST, OUTPUT);
    ledcSetup(BLOWER_CH, BLOWER_FREQ, BLOWER_RES);  // 1 kHz: spek XY-MOS 0-20 kHz
    ledcAttachPin(BLOWER_PIN, BLOWER_CH);

    // 4 pompa irigasi di-drive state machine irrigation.cpp; pastikan mati di awal.
    for (int pin : {PUMP_FILL, PUMP_FERT_A, PUMP_FERT_B, PUMP_DIST}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, MOSFET_INVERTED ? HIGH : LOW);   // LOW = off (aktif-HIGH)
    }
    setActuators(false, false, 0.0);
}