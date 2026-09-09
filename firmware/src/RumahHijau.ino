#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "sensor.h"
#include "vpd.h"
#include "irrigation.h"
#include "fuzzy.h"
#include "actuator.h"
#include "control.h"
#include "telegram_bot.h"
#include "firebase_db.h"
#include "oled_display.h"

static unsigned long lastOled = 0;
static unsigned long lastBot = 0;
static unsigned long lastPush = 0;
static unsigned long lastCmd = 0;

static SensorData sensorData;
static IrrigationResult irrigation;
static float vpdValue = 0;
static const char* systemStatus = "Starting";
static FuzzyOutput fuzzyOut;

void connectWiFi() {
    WiFi.setTxPower(WIFI_POWER_11dBm);
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
        delay(100);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" Connected!");
        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println(" WiFi timeout, continuing offline");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== Smart Watering System ===");
    Serial.println("Greenhouse IoT - Chili Cultivation");

    initActuators();
    controlRelease();
    initOled();
    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
        initTelegram();
        // WIB (UTC+7). ts di sensor_logs jadi epoch asli agar waktu dashboard akurat.
        configTime(7 * 3600, 0, "pool.ntp.org");
    }

    initSensor();
    delay(2000);

    Serial.println("System initialized!");
    lastOled = millis();
    lastBot = millis();
}

void loop() {
    float soilRaw = analogRead(SOIL_PIN);
    sensorData = readSensors(soilRaw);

    if (sensorData.valid) {
        vpdValue = calculateVPD(sensorData.temperature, sensorData.humidity);
        irrigation = calculateIrrigation(sensorData.temperature,
                                         sensorData.soilMoisture);
        fuzzyOut = fuzzyDecision(sensorData.temperature, sensorData.humidity,
                                 sensorData.soilMoisture, vpdValue);
        systemStatus = fuzzyOut.status;

        if (sensorData.temperature > 38.0) {
            sendTelegramAlert("TEMPERATUR SANGAT TINGGI!");
        } else if (sensorData.temperature < 10.0) {
            sendTelegramAlert("TEMPERATUR SANGAT RENDAH!");
        }
    } else {
        static unsigned long lastFailPrint = 0;
        if (millis() - lastFailPrint > 5000) {
            lastFailPrint = millis();
            Serial.println("[!] Sensor DHT22/Soil tidak terhubung - cek wiring");
            Serial.printf("[!] DHT error: %s (T:%.1f H:%.1f)\n",
                          getLastDHTError(), sensorData.temperature, sensorData.humidity);
            Serial.printf("[!] Soil raw: %d\n", (int)soilRaw);
        }
    }

    bool mist = fuzzyOut.mistOn;
    bool irigasi = irrigation.shouldIrrigate;
    bool blower = fuzzyOut.blowerOn;
    controlApply(&mist, &irigasi, &blower);
    setActuators(mist, irigasi, blower);

    if (sensorData.valid) {
        Serial.printf("T:%.1fC H:%.1f%% Soil:%.1f%% VPD:%.2fkPa ETc:%.1fmm [%s]",
                      sensorData.temperature, sensorData.humidity,
                      sensorData.soilMoisture, vpdValue, irrigation.etc, systemStatus);
        Serial.printf(" Mist:%s Irig:%s Blower:%s\n",
                      mist ? "ON" : "OFF", irigasi ? "ON" : "OFF", blower ? "ON" : "OFF");
    }

    unsigned long now = millis();
    if (now - lastOled >= OLED_INTERVAL) {
        lastOled = now;
        updateOled(sensorData.temperature, sensorData.humidity,
                  sensorData.soilMoisture, vpdValue, systemStatus);
    }

    if (now - lastBot >= BOT_CHECK_INTERVAL && WiFi.status() == WL_CONNECTED) {
        lastBot = now;
        pollTelegram(sensorData.temperature, sensorData.humidity,
                    sensorData.soilMoisture, vpdValue, systemStatus,
                    mist, irigasi, blower);
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (sensorData.valid && now - lastPush >= DB_PUSH_MS) {
            lastPush = now;
            String statusSistem = String(systemStatus);
            if (mist) statusSistem += " MISTING";
            if (blower) statusSistem += " BLOWER";
            if (irigasi) statusSistem += " IRIGASI";
            pushSensorLog(sensorData.temperature, sensorData.humidity,
                          sensorData.soilMoisture, vpdValue, irrigation.etc, statusSistem.c_str());
        }
        if (now - lastCmd >= DB_POLL_MS) {
            lastCmd = now;
            pollCommands();
        }
    }

    delay(2000);
}