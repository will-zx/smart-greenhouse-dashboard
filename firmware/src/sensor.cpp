#include "sensor.h"
#include <Wire.h>
#include <Adafruit_SHT31.h>

static Adafruit_SHT31 sht31 = Adafruit_SHT31();
static bool shtOk = false;

void initSensor() {
    Wire.begin(OLED_SDA, OLED_SCL);
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
    data.soilMoisture = constrain(soilRaw, SOIL_WET, SOIL_DRY);
    data.soilMoisture = map((int)data.soilMoisture, SOIL_DRY, SOIL_WET, 0, 100);
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