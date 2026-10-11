#include <Arduino.h>
#include <WiFi.h>
#include <freertos/semphr.h>
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

// ==== Arsitektur 2 task (FreeRTOS) ====
// controlTask (core 1): sensor -> VPD/ETc -> fuzzy -> aktuator -> irigasi -> LCD -> SD.
// netTask (core 0): WiFi, token, push RTDB, poll perintah, backlog SD.
// Tujuan: I/O jaringan (TLS bisa beberapa detik, WiFi join s/d WIFI_TIMEOUT)
// tidak lagi menahan jalur aktuator — pulse pompa FERT 2s/1s tidak lagi
// korup karena loop tertahan saat HTTP.
static const int CONTROL_TASK_STACK = 4096;
static const int NET_TASK_STACK = 10240;   // TLS mbedTLS butuh stack besar
static const int CONTROL_TASK_PRIO = 3;
static const int NET_TASK_PRIO = 2;
static const int CONTROL_TASK_CORE = 1;    // core 0 dipakai WiFi/lwIP
static const int NET_TASK_CORE = 0;

// ==== Jembatan controlTask -> netTask ====
// Satu struct kecil di bawah mutex: kontrol menulis, jaringan membaca.
struct Snapshot {
    float temperature, humidity, soilMoisture, vpd, etc, tds;
    bool  valid, manual, mist, blower, irigasiRunning;
    char  status[40];   // sudah termasuk suffix " MISTING"/" BLOWER"/" IRIGASI"
};

static Snapshot gSnap;
static SemaphoreHandle_t snapMux = nullptr;

static void snapshotPublish(const Snapshot& s) {
    xSemaphoreTake(snapMux, portMAX_DELAY);
    gSnap = s;
    xSemaphoreGive(snapMux);
}

static Snapshot snapshotRead() {
    Snapshot s;
    xSemaphoreTake(snapMux, portMAX_DELAY);
    s = gSnap;
    xSemaphoreGive(snapMux);
    return s;
}

// ==== WiFi ====
static unsigned long lastReconnect = 0;

void connectWiFi() {
    WiFi.setTxPower(WIFI_POWER_11dBm);
    // Modem-sleep bikin handshake TLS sering putus (SSL EOF) di AP tertentu.
    WiFi.setSleep(false);
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
        vTaskDelay(pdMS_TO_TICKS(100));
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

// ==== Task kontrol (core 1, prio 3) ====
static void controlTask(void* arg) {
    SensorData sensorData;
    float vpdValue = 0;
    float etcValue = 0;
    float tdsPpm = 0;
    const char* systemStatus = "Starting";
    FuzzyOutput fuzzyOut;
    unsigned long lastLcd = 0;
    bool alertActive = false;
    unsigned long lastFailPrint = 0;
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        float soilRaw = analogRead(SOIL_PIN);
        sensorData = readSensors(soilRaw);
        tdsPpm = readTDS(sensorData.temperature);

        if (sensorData.valid) {
            vpdValue = calculateVPD(sensorData.temperature, sensorData.humidity);
            etcValue = calculateEtc(sensorData.temperature);
            fuzzyOut = fuzzyDecision(sensorData.temperature, sensorData.humidity,
                                     sensorData.soilMoisture, vpdValue, etcValue);
            systemStatus = fuzzyOut.status;

            // Peringatan suhu ekstrem (serial + LCD; Telegram sudah dihapus).
            bool nowAlert = sensorData.temperature > 38.0 || sensorData.temperature < 10.0;
            if (nowAlert && !alertActive) {
                Serial.printf("[!] ALERT: suhu ekstrem %.1f C\n", sensorData.temperature);
            }
            alertActive = nowAlert;
        } else {
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
        bool manual = controlIsManual();
        // Manual = 100%; auto = level fuzzy (presisi VPD via PWM).
        float blowerLevel = manual ? (blower ? 1.0 : 0.0) : fuzzyOut.blowerLevel;
        setActuators(mist, blower, blowerLevel);

        // Irigasi 4-pompa: manual OFF = batalkan siklus; selain itu serah ke fuzzy
        // (edge-trigger di irrigationUpdate — cycle sekali per permintaan).
        if (manual && !irigasi) {
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
                         systemStatus, (int)manual);
                sdLogRecord(line);
            }
        }

        unsigned long now = millis();
        if (now - lastLcd >= LCD_INTERVAL) {
            lastLcd = now;
            updateLcd(sensorData.temperature, sensorData.humidity,
                      sensorData.soilMoisture, vpdValue, etcValue, tdsPpm, systemStatus);
        }

        // Publikasikan ke netTask. status_sistem lengkap dengan suffix aktuator —
        // itu yang dipakai dashboard untuk posisi switch & pill.
        Snapshot s = {};
        s.temperature = sensorData.temperature;
        s.humidity = sensorData.humidity;
        s.soilMoisture = sensorData.soilMoisture;
        s.vpd = vpdValue;
        s.etc = etcValue;
        s.tds = tdsPpm;
        s.valid = sensorData.valid;
        s.manual = manual;
        s.mist = mist;
        s.blower = blower;
        s.irigasiRunning = irigasiRunning;
        snprintf(s.status, sizeof(s.status), "%s", systemStatus);
        if (mist) strncat(s.status, " MISTING", sizeof(s.status) - strlen(s.status) - 1);
        if (blower) strncat(s.status, " BLOWER", sizeof(s.status) - strlen(s.status) - 1);
        if (irigasiRunning) strncat(s.status, " IRIGASI", sizeof(s.status) - strlen(s.status) - 1);
        snapshotPublish(s);

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(2000));
    }
}

// ==== Task jaringan (core 0, prio 2) ====
static void netTask(void* arg) {
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) syncTime();

    unsigned long lastPush = 0;
    unsigned long lastCmd = 0;

    for (;;) {
        unsigned long now = millis();

        // Reconnect: tanpa ini, sekali WiFi putus perangkat offline permanen
        // sampai reset manual. Blocking s/d WIFI_TIMEOUT hanya menahan task ini —
        // kontrol lokal & aktuator tetap jalan.
        if (WiFi.status() != WL_CONNECTED && now - lastReconnect >= WIFI_RETRY_MS) {
            Serial.println("[wifi] putus, mencoba reconnect...");
            connectWiFi();
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("[wifi] tersambung kembali");
                syncTime();
            }
        }

        Snapshot s = snapshotRead();

        if (WiFi.status() == WL_CONNECTED) {
            if (now - lastCmd >= DB_POLL_MS) {
                lastCmd = now;
                pollCommands();
            }
            if (s.valid && now - lastPush >= DB_PUSH_MS) {
                lastPush = now;
                String body;
                bool ok = pushSensorLog(s.temperature, s.humidity, s.soilMoisture,
                                        s.vpd, s.etc, s.tds, s.status, s.manual, &body);
                if (ok) {
                    sdFlushBacklog();   // pulih: kirim antrean yang tertunda
                } else if (body.length() > 0) {
                    sdQueueBacklog(body.c_str());  // gagal: antre ke MicroSD
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== Smart Watering System ===");
    Serial.println("Greenhouse IoT - Chili Cultivation");
    Serial.print("Device ID: ");
    Serial.println(DEVICE_ID);

    initActuators();
    controlInit();
    controlRelease();
    initLcd();
    initSdLogger();
    initSensor();
    delay(2000);

    snapMux = xSemaphoreCreateMutex();

    Serial.println("System initialized!");
    xTaskCreatePinnedToCore(controlTask, "control", CONTROL_TASK_STACK, nullptr,
                            CONTROL_TASK_PRIO, nullptr, CONTROL_TASK_CORE);
    xTaskCreatePinnedToCore(netTask, "net", NET_TASK_STACK, nullptr,
                            NET_TASK_PRIO, nullptr, NET_TASK_CORE);
}

// Semua kerja ada di task; loop() tinggal idle.
void loop() {
    vTaskDelay(portMAX_DELAY);
}
