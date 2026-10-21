#ifndef TDS_H
#define TDS_H

#include <Arduino.h>

// TDS larutan irigasi (ppm). temperatureC untuk kompensasi suhu probe
// (pakai pembacaan SHT31). Catatan: TDS sekarang hanya telemetri; dosis
// pupuk (pompa FERT) ditunda, jadi nilainya tidak dipakai menghitung dosis.
// *valid = false bila raw ADC di luar rentang wajar (probe dicabut / mengambang
// / tidak terendam) atau ppm di luar batas -> angka tidak boleh dipakai untuk
// menghitung dosis pupuk.
float readTDS(float temperatureC, bool* valid);

#endif
