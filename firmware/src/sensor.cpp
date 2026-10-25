#include "sensor.h"
#include <Wire.h>
#include <Adafruit_SHT31.h>

static Adafruit_SHT31 sht31 = Adafruit_SHT31();
static bool shtOk = false;

// Salinan SOIL_PINS / SOIL_*_CAL dari config.h. Array (bukan #define langsung
// di dalam loop) supaya satu loop bisa menangani tiga probe tanpa duplikasi.
static const uint8_t soilPins[SOIL_COUNT] = SOIL_PINS;
static const int     soilDry[SOIL_COUNT]  = SOIL_DRY_CAL;
static const int     soilWet[SOIL_COUNT]  = SOIL_WET_CAL;

void initSensor() {
    Wire.begin(I2C_SDA, I2C_SCL);
    shtOk = sht31.begin(SHT31_I2C_ADDR);
    if (shtOk) {
        Serial.println("[sensor] SHT31 terdeteksi");
    } else {
        Serial.println("[sensor] SHT31 TIDAK terdeteksi - cek wiring I2C (addr 0x44)");
    }
    for (uint8_t ch = 0; ch < SOIL_COUNT; ch++) {
        pinMode(soilPins[ch], INPUT);
    }
}

// Rata-rata SOIL_SAMPLES bacaan satu kanal; sampel pertama dibuang karena ADC
// masih membawa sisa muatan kanal sebelumnya (lihat SOIL_SAMPLES di config.h).
static float readSoilRaw(uint8_t ch) {
    analogRead(soilPins[ch]);
    float sum = 0.0f;
    for (int s = 0; s < SOIL_SAMPLES; s++) sum += (float)analogRead(soilPins[ch]);
    return sum / (float)SOIL_SAMPLES;
}

// Raw -> persen untuk satu probe. false = angka tidak masuk akal (probe
// dicabut / kabel putus / salah pin): ADC mengambang 0 atau 4095, di luar
// rentang kalibrasi, sehingga 0 %/100 % palsu tidak boleh dipakai keputusan.
static bool soilPercent(uint8_t ch, float raw, float* out) {
    float lo = (float)soilWet[ch] - (float)SOIL_RAW_TOL;
    float hi = (float)soilDry[ch] + (float)SOIL_RAW_TOL;
    if (isnan(raw) || raw < lo || raw > hi) return false;
    // ADC terbalik: kering = raw tinggi. constrain dulu supaya map tidak
    // menghasilkan negatif/>100 saat raw sedikit melewati titik kalibrasi.
    float clamped = constrain(raw, (float)soilWet[ch], (float)soilDry[ch]);
    *out = (float)map((long)clamped, soilDry[ch], soilWet[ch], 0, 100);
    return true;
}

SensorData readSensors() {
    SensorData data;
    data.temperature = sht31.readTemperature();
    data.humidity = sht31.readHumidity();

    // Tiga probe dibaca bergantian. Keputusan irigasi memakai RATA-RATA probe
    // yang valid, sedangkan tiap probe tetap dilaporkan (soil2/soil3) supaya
    // pot/polybag yang mengering lebih dulu kelihatan di dashboard.
    float sum = 0.0f;
    int   n = 0;
    for (uint8_t ch = 0; ch < SOIL_COUNT; ch++) {
        float raw = readSoilRaw(ch);
        float pct = NAN;
        data.soilRaw[ch] = raw;
        if (soilPercent(ch, raw, &pct)) {
            sum += pct;
            n++;
        }
        data.soilPct[ch] = pct;
        if (ch + 1 < SOIL_COUNT) delay(SOIL_SETTLE_MS);
    }
    data.soilValid = (n > 0);
    data.soilMoisture = (n > 0) ? (sum / (float)n) : 0.0f;

    data.valid = shtOk && !isnan(data.temperature) && !isnan(data.humidity) &&
                 data.temperature > -20.0 && data.temperature < 60.0 &&
                 data.humidity >= 0.0 && data.humidity <= 100.0;
    return data;
}

const char* getSensorError() {
    if (!shtOk) return "SHT31 tidak terdeteksi";
    if (isnan(sht31.readTemperature())) return "read gagal (bus I2C?)";
    return "OK";
}
