#include "actuator.h"

void setActuators(bool mist, bool irigasi, bool blower) {
    digitalWrite(RELAY_MIST, mist ? LOW : HIGH);
    digitalWrite(RELAY_IRRIG, irigasi ? LOW : HIGH);
    digitalWrite(RELAY_BLOWER, blower ? LOW : HIGH);
}

void initActuators() {
    pinMode(RELAY_MIST, OUTPUT);
    pinMode(RELAY_IRRIG, OUTPUT);
    pinMode(RELAY_BLOWER, OUTPUT);
    setActuators(false, false, false);
}