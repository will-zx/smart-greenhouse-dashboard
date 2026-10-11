#include "control.h"

static bool active = false;
static bool manMist = false;
static bool manIrig = false;
static bool manBlower = false;

void controlRelease() {
    active = false;
    manMist = false;
    manIrig = false;
    manBlower = false;
}

void controlSetMist(bool on) {
    manMist = on;
    active = true;
}

void controlSetIrig(bool on) {
    manIrig = on;
    active = true;
}

void controlSetBlower(bool on) {
    manBlower = on;
    active = true;
}

void controlApply(bool* mist, bool* irigasi, bool* blower) {
    if (!active) return;
    *mist = manMist;
    *irigasi = manIrig;
    *blower = manBlower;
}

bool controlIsManual() {
    return active;
}