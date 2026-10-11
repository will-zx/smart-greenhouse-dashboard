#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "sensor.h"
#include "vpd.h"
#include "irrigation.h"
#include "fuzzy.h"
#include "actuator.h"
#include "control.h"
#include "tds.h"
#include "firebase_db.h"
#include "sd_logger.h"
#include "lcd_display.h"

static unsigned long lastLcd = 0;
static unsigned long lastPush = 0;
static unsigned long lastCmd = 0;
static unsigned long lastReconnect = 0;

static SensorData sensorData;
static float etcValue = 0;
static float vpdValue = 0;
static const char* systemStatus = "Starting";
static FuzzyOutput fuzzyOut;

void connectWiFi() {
    WiFi.setTxPower(WIFI_POWER_11dBm);
    // Modem-sleep bikin handshake TLS sering putus (SSL EOF) di AP tertentu.
    WiFi.setSleep(false);
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
    lastReconnect = millis();
}

void syncTime() {
    // WIB (UTC+7). ts di sensor_logs jadi epoch asli agar waktu dashboard akurat.
    configTime(7 * 3600, 0, "pool.ntp.org");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== Smart Watering System ===");
    Serial.println("Greenhouse IoT - Chili Cultivation");
    Serial.print("Device ID: ");
    Serial.println(DEVICE_ID);

    initActuators();
    controlRelease();
    initLcd();
    initSdLogger();
    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) syncTime();

    initSensor();
    delay(2000);

    Serial.println("System initialized!");
    lastLcd = millis();
}

void loop() {
    float soilRaw = analogRead(SOIL_PIN);
    sensorData = readSensors(soilRaw);
    float tdsPpm = readTDS(sensorData.temperature);

    if (sensorData.valid) {
        vpdValue = calculateVPD(sensorData.temperature, sensorData.humidity);
        etcValue = calculateEtc(sensorData.temperature);
        fuzzyOut = fuzzyDecision(sensorData.temperature, sensorData.humidity,
                                 sensorData.soilMoisture, vpdValue, etcValue);
        systemStatus = fuzzyOut.status;

        // Peringatan suhu ekstrem (serial + LCD; Telegram sudah dihapus).
        static bool alertActive = false;
        bool nowAlert = sensorData.temperature > 38.0 || sensorData.temperature < 10.0;
        if (nowAlert && !alertActive) {
            Serial.printf("[!] ALERT: suhu ekstrem %.1f C\n", sensorData.temperature);
        }
        alertActive = nowAlert;
    } else {
        static unsigned long lastFailPrint = 0;
        if (millis() - lastFailPrint > 5000) {
            lastFailPrint = millis();
            Serial.println("[!] Sensor SHT31/Soil tidak terhubung - cek wiring");
            Serial.printf("[!] Sensor error: %s (T:%.1f H:%.1f)\n",
                          getSensorError(), sensorData.temperature, sensorData.humidity);
            Serial.printf("[!] Soil raw: %d\n", (int)soilRaw);
        }
    }

    bool mist = fuzzyOut.mistOn;
    bool irigasi = fuzzyOut.irigasiOn;   // keputusan fuzzy; manual via controlApply
    bool blower = fuzzyOut.blowerOn;
    controlApply(&mist, &irigasi, &blower);
    // Manual = 100%; auto = level fuzzy (presisi VPD via PWM).
    float blowerLevel = controlIsManual() ? (blower ? 1.0 : 0.0) : fuzzyOut.blowerLevel;
    setActuators(mist, blower, blowerLevel);

    // Irigasi 4-pompa: manual OFF = batalkan siklus; selain itu serah ke fuzzy
    // (edge-trigger di irrigationUpdate — cycle sekali per permintaan).
    if (controlIsManual() && !irigasi) {
        irrigationAbort();
    } else {
        irrigationUpdate(irigasi, tdsPpm);
    }
    bool irigasiRunning = irrigationRunning();

    if (sensorData.valid) {
        Serial.printf("T:%.1fC H:%.1f%% Soil:%.1f%% VPD:%.2fkPa ETc:%.1fmm TDS:%.0fppm [%s]",
                      sensorData.temperature, sensorData.humidity,
                      sensorData.soilMoisture, vpdValue, etcValue, tdsPpm, systemStatus);
        Serial.printf(" Mist:%s Irig:%s Blower:%s(lv%.0f%%)\n",
                      mist ? "ON" : "OFF", irigasiRunning ? "RUN" : "OFF",
                      blower ? "ON" : "OFF", blowerLevel * 100.0);

        // Backup lokal: seluruh pembacaan valid dicatat ke MicroSD.
        time_t ts = time(nullptr);
        if (ts >= 100000) {
            char line[128];
            snprintf(line, sizeof(line), "%ld,%.1f,%.1f,%.1f,%.2f,%.1f,%s,%d",
                     (long)ts, sensorData.temperature, sensorData.humidity,
                     sensorData.soilMoisture, vpdValue, etcValue,
                     systemStatus, (int)controlIsManual());
            sdLogRecord(line);
        }
    }

    unsigned long now = millis();

    // Reconnect: tanpa ini, sekali WiFi putus perangkat offline permanen
    // sampai reset manual (kontrol lokal tetap jalan, hanya push/poll yang mati).
    // ponytail: connectWiFi() blocking s/d WIFI_TIMEOUT, jadi loop ikut tertahan
    // saat sedang mencoba. Jika aktuator harus responsif saat WiFi mati,
    // pindahkan ke task FreeRTOS terpisah.
    if (WiFi.status() != WL_CONNECTED && now - lastReconnect >= WIFI_RETRY_MS) {
        Serial.println("[wifi]putus, mencoba reconnect...");
        connectWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[wifi] tersambung kembali");
            syncTime();
        }
    }

    if (now - lastLcd >= LCD_INTERVAL) {
        lastLcd = now;
        updateLcd(sensorData.temperature, sensorData.humidity,
                  sensorData.soilMoisture, vpdValue, etcValue, tdsPpm, systemStatus);
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (sensorData.valid && now - lastPush >= DB_PUSH_MS) {
            lastPush = now;
            String statusSistem = String(systemStatus);
            if (mist) statusSistem += " MISTING";
            if (blower) statusSistem += " BLOWER";
            if (irigasiRunning) statusSistem += " IRIGASI";
            String body;
            bool ok = pushSensorLog(sensorData.temperature, sensorData.humidity,
                                    sensorData.soilMoisture, vpdValue, etcValue, tdsPpm,
                                    statusSistem.c_str(), controlIsManual(), &body);
            if (ok) {
                sdFlushBacklog();   // pulih: kirim antrean yang tertunda
            } else if (body.length() > 0) {
                sdQueueBacklog(body.c_str());  // gagal: antre ke MicroSD
            }
        }
        if (now - lastCmd >= DB_POLL_MS) {
            lastCmd = now;
            pollCommands();
        }
    }

    delay(2000);
}