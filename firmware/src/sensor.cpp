#include "sensor.h"
#include <Wire.h>
#include <Adafruit_SHT31.h>

static Adafruit_SHT31 sht31 = Adafruit_SHT31();
static bool shtOk = false;

void initSensor() {
    Wire.begin(I2C_SDA, I2C_SCL);
    shtOk = sht31.begin(SHT31_I2C_ADDR);
    if (shtOk) {
        Serial.println("[sensor] SHT31 terdeteksi");
    } else {
        Serial.println("[sensor] SHT31 TIDAK terdeteksi - cek wiring I2C (addr 0x44)");
    }
    pinMode(SOIL_PIN, INPUT);
}

SensorData readSensors(float soilRaw) {
    SensorData data;
    data.temperature = sht31.readTemperature();
    data.humidity = sht31.readHumidity();

    // Validasi probe tanah. Probe dicabut -> ADC mengambang (0 atau 4095),
    // yang dipetakan jadi "100 % basah" (irigasi mati diam-diam) atau
    // "0 % kering" (irigasi terus jalan). Jadi raw di luar rentang kalibrasi
    // -> soilValid = false dan TIDAK boleh dipakai untuk keputusan irigasi.
    // ponytail: SOIL_DRY/SOIL_WET masih ASUMSI (belum diukur pada probe asli)
    // -> rentang valid ikut bergeser; ukur titik kering/basah lalu perbarui.
    float lo = (float)SOIL_WET - (float)SOIL_RAW_TOL;
    float hi = (float)SOIL_DRY + (float)SOIL_RAW_TOL;
    data.soilValid = !isnan(soilRaw) && soilRaw >= lo && soilRaw <= hi;

    if (data.soilValid) {
        data.soilMoisture = constrain(soilRaw, SOIL_WET, SOIL_DRY);
        data.soilMoisture = map((int)data.soilMoisture, SOIL_DRY, SOIL_WET, 0, 100);
    } else {
        // Nilai carry-over (bukan 0/100 palsu): pemanggil memakai nilai
        // terakhir yang sah dan menahan irigasi.
        data.soilMoisture = 0.0f;
    }

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