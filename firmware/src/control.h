#ifndef CONTROL_H
#define CONTROL_H

#include <Arduino.h>

void controlInit();   // buat mutex — panggil sebelum task dibuat
void controlRelease();
void controlSetMist(bool on);
void controlSetIrig(bool on);
void controlSetBlower(bool on);
void controlApply(bool* mist, bool* irigasi, bool* blower);
bool controlIsManual();

#endif
