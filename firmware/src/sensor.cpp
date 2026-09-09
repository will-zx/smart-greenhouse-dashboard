#include "sensor.h"
#include <DHTesp.h>

static DHTesp dht;

void initSensor() {
    dht.setup(DHT_PIN, DHTesp::DHT22);
    pinMode(SOIL_PIN, INPUT);
}

SensorData readSensors(float soilRaw) {
    SensorData data;
    TempAndHumidity th = dht.getTempAndHumidity();
    data.temperature = th.temperature;
    data.humidity = th.humidity;
    data.soilMoisture = constrain(soilRaw, SOIL_WET, SOIL_DRY);
    data.soilMoisture = map((int)data.soilMoisture, SOIL_DRY, SOIL_WET, 0, 100);
    data.valid = data.temperature > -20.0 && data.temperature < 60.0 &&
                 data.humidity >= 0.0 && data.humidity <= 100.0;
    return data;
}

const char* getLastDHTError() {
    if (dht.getStatus() != DHTesp::ERROR_NONE) {
        return dht.getStatusString();
    }
    return "OK";
}