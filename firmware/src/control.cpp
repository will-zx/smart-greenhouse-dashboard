#include "control.h"
#include <freertos/semphr.h>

// State override manual. Ditulis netTask (poll perintah RTDB) dan dibaca
// controlTask -> dilindungi mutex supaya satu iterasi fuzzy tidak melihat
// campuran nilai lama/baru.
static SemaphoreHandle_t mux = nullptr;
static bool active = false;
static bool manMist = false;
static bool manIrig = false;
static bool manBlower = false;

static void lock()   { if (mux) xSemaphoreTake(mux, portMAX_DELAY); }
static void unlock() { if (mux) xSemaphoreGive(mux); }

void controlInit() {
    if (!mux) mux = xSemaphoreCreateMutex();
}

void controlRelease() {
    lock();
    active = false;
    manMist = false;
    manIrig = false;
    manBlower = false;
    unlock();
}

void controlSetMist(bool on) {
    lock();
    manMist = on;
    active = true;
    unlock();
}

void controlSetIrig(bool on) {
    lock();
    manIrig = on;
    active = true;
    unlock();
}

void controlSetBlower(bool on) {
    lock();
    manBlower = on;
    active = true;
    unlock();
}

void controlApply(bool* mist, bool* irigasi, bool* blower) {
    lock();
    if (active) {
        *mist = manMist;
        *irigasi = manIrig;
        *blower = manBlower;
    }
    unlock();
}

bool controlIsManual() {
    lock();
    bool r = active;
    unlock();
    return r;
}
