#include "vpd.h"

float calculateVPD(float temp, float humidity) {
    float svp = 0.6108 * exp((17.27 * temp) / (temp + 237.3));
    float avp = svp * (humidity / 100.0);
    return svp - avp;
}