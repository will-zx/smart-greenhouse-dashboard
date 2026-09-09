#ifndef ACTUATOR_H
#define ACTUATOR_H

#include <Arduino.h>
#include "config.h"

void initActuators();
void setActuators(bool mist, bool irigasi, bool blower);

#endif