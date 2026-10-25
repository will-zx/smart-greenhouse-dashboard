#include "control.h"
#include "config.h"
#include <freertos/semphr.h>

// State override manual. Ditulis netTask (poll perintah RTDB) dan dibaca
// controlTask -> dilindungi mutex supaya satu iterasi fuzzy tidak melihat
// campuran nilai lama/baru.
//
// Override TRIS-TATA per aktuator: -1 = ikut fuzzy, 0 = paksa mati,
// 1 = paksa nyala. Dulu override memakai satu bitmask untuk semua aktuator,
// jadi mematikan satu sakelar mematikan semuanya (bug paling berbahaya).
// Setiap aktuator punya jam sendiri: batas durasi override dihitung per
// aktuator, jadi override yang baru saja di-set tidak langsung ikut kedaluwarsa.
static SemaphoreHandle_t mux = nullptr;
static int8_t      ovMist = -1, ovIrig = -1, ovBlower = -1;
static unsigned long tMist = 0, tIrig = 0, tBlower = 0;   // millis override aktif

static void lock()   { if (mux) xSemaphoreTake(mux, portMAX_DELAY); }
static void unlock() { if (mux) xSemaphoreGive(mux); }

static bool anyOverride() {
    return ovMist >= 0 || ovIrig >= 0 || ovBlower >= 0;
}

// Batas durasi override manual per aktuator: lewat MANUAL_MAX_MS aktuator itu
// kembali ke fuzzy (fail-safe: misting/irigasi tidak boleh nyala semalaman).
static void expireManual() {
    unsigned long now = millis();
    const char* nama = "";
    if (ovMist >= 0 && now - tMist >= MANUAL_MAX_MS) {
        ovMist = -1; tMist = 0; nama = "misting";
    } else if (ovIrig >= 0 && now - tIrig >= MANUAL_MAX_MS) {
        ovIrig = -1; tIrig = 0; nama = "irigasi";
    } else if (ovBlower >= 0 && now - tBlower >= MANUAL_MAX_MS) {
        ovBlower = -1; tBlower = 0; nama = "blower";
    } else {
        return;
    }
    Serial.printf("[ctl] override manual %s habis (%lu menit) - kembali ke fuzzy\n",
                  nama, (unsigned long)(MANUAL_MAX_MS / 60000UL));
}

void controlInit() {
    if (!mux) mux = xSemaphoreCreateMutex();
}

void controlRelease() {
    lock();
    ovMist = ovIrig = ovBlower = -1;
    tMist = tIrig = tBlower = 0;
    unlock();
    Serial.println("[ctl] auto: semua aktuator kembali ke kendali fuzzy");
}

void controlForce(uint8_t bit, int8_t mode) {
    if (mode < -1) mode = -1;
    if (mode > 1)  mode = 1;
    lock();
    // Mode 0 (paksa mati) WAJIB distempel juga: expireManual() memakai
    // selisih millis() - tX, jadi stempel 0 membuat override mati tidak pernah
    // kedaluwarsa (sekaligus menghindari wrap-up di awal boot).
    unsigned long stamp = (mode == -1) ? 0 : millis();
    const char* nama = "";
    if (bit & C_MIST)   { ovMist = mode; tMist = stamp; nama = "misting"; }
    if (bit & C_IRIG)   { ovIrig = mode; tIrig = stamp; nama = "irigasi"; }
    if (bit & C_BLOWER) { ovBlower = mode; tBlower = stamp; nama = "blower"; }
    unlock();
    Serial.printf("[ctl] override %s -> %s\n", nama,
                  mode == 1 ? "paksa nyala"
                            : (mode == 0 ? "paksa mati" : "kembali ke fuzzy"));
}

void controlSet(uint8_t bit, bool on) {
    // Sakelar dimatikan = aktuator itu kembali ke fuzzy, BUKAN dipaksa mati
    // dan bukan ikut memadamkan aktuator lain.
    controlForce(bit, on ? 1 : -1);
}

void controlApply(bool* mist, bool* irigasi, bool* blower, float* blowerLevel) {
    lock();
    expireManual();
    if (ovMist >= 0)   *mist   = (ovMist == 1);
    if (ovIrig >= 0)   *irigasi = (ovIrig == 1);
    if (ovBlower >= 0) {
        *blower = (ovBlower == 1);
        if (blowerLevel) *blowerLevel = (ovBlower == 1) ? 1.0f : 0.0f;
    }
    unlock();
}

bool controlIsManual() {
    lock();
    expireManual();
    bool r = anyOverride();
    unlock();
    return r;
}

OverrideState controlOverrides() {
    lock();
    expireManual();
    OverrideState ov = { ovMist, ovIrig, ovBlower };
    unlock();
    return ov;
}
