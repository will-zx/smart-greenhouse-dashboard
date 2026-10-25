#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

struct SensorData {
    float temperature;
    float humidity;
    float soilMoisture;            // rata-rata probe yang valid (%)
    float soilPct[SOIL_COUNT];     // per probe (%), NAN bila probe tidak masuk akal
    float soilRaw[SOIL_COUNT];     // ADC mentah per probe (untuk kalibrasi)
    bool  valid;        // SHT31 (mikroklimat) terbaca & masuk rentang
    bool  soilValid;    // minimal satu probe tanah dalam rentang raw yang masuk akal
    // Status per probe. soilPct[]/soilRaw[] berisi NAN saat probe gagal, dan
    // field itu dibuang dari JSON (putNum) — jadi dashboard tidak bisa
    // membedakan "probe tercabut" dari "record firmware lama". Dua angka ini
    // dikirim eksplisit supaya bedanya jelas.
    uint8_t soilOk;     // bitmask probe valid: bit0=SOIL_PINS[0], bit1=..[1], dst
    uint8_t soilCount;  // jumlah probe valid (0..SOIL_COUNT)
};

void initSensor();
SensorData readSensors();
const char* getSensorError();

// Bus I2C dipakai bersama SHT31 (0x44) dan LCD (0x27); Wire tidak thread-safe.
// Setiap transaksi WAJIB lewat i2cLock()/i2cUnlock(). Tanpa ini task scroll LCD
// bisa menimpa pembacaan SHT31 -> suhu/RH NaN -> "Sensor Error" acak.
// Timeout pendek (mis. pdMS_TO_TICKS(50)) dan kalau gagal: LEWATI transaksi itu,
// jangan menunggu tanpa batas.
void i2cBusInit();                       // sekali di setup(), sebelum initLcd()
bool i2cLock(TickType_t timeout);
void i2cUnlock();

#endif
