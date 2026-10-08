#ifndef TDS_H
#define TDS_H

#include <Arduino.h>

// TDS larutan di ember campuran irigasi (ppm). temperatureC untuk kompensasi
// suhu probe (pakai pembacaan SHT31).
float readTDS(float temperatureC);

#endif