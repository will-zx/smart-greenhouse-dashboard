#include "control.h"
#include <freertos/semphr.h>

// State override manual. Ditulis netTask (poll perintah RTDB) dan dibaca
// controlTask -> dilindungi mutex supaya satu iterasi fuzzy tidak melihat
// campuran nilai lama/baru.
static SemaphoreHandle_t mux = nullptr;
static bool     active = false;
static uint8_t  manual = 0;

static void lock()   { if (mux) xSemaphoreTake(mux, portMAX_DELAY); }
static void unlock() { if (mux) xSemaphoreGive(mux); }

void controlInit() {
    if (!mux) mux = xSemaphoreCreateMutex();
}

void controlRelease() {
    lock();
    active = false;
    manual = 0;
    unlock();
}

void controlSet(uint8_t bit, bool on) {
    lock();
    if (on) manual |= bit; else manual &= ~bit;
    active = true;
    unlock();
}

void controlApply(bool* mist, bool* irigasi, bool* blower) {
    lock();
    if (active) {
        *mist    = manual & C_MIST;
        *irigasi = manual & C_IRIG;
        *blower  = manual & C_BLOWER;
    }
    unlock();
}

bool controlIsManual() {
    lock();
    bool r = active;
    unlock();
    return r;
}