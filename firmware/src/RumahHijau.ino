#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/semphr.h>
#include "config.h"
#include "sensor.h"
#include "filter.h"
#include "phase.h"
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
    bool  manual, mist, blower, irigasiRunning;
    float blowerLevel;  // 0..1, tingkat blower yang benar-benar dijalankan (PWM)
    char  status[64];   // sudah termasuk suffix " MISTING"/" BLOWER"/" IRIGASI"
    PhaseState phase;   // setelan fase + VPD target (untuk telemetri DB)
};

static Snapshot gSnap;
static SemaphoreHandle_t snapMux = nullptr;

// Filter EMA sinyal sensor (lihat filter.h). Alfa dari config.h.
static EmaFilter tempFilter, humFilter, soilFilter, tdsFilter;

// VPD (Tetens): kPa = SVP - AVP.
static float calcVpd(float temp, float humidity) {
    float svp = 0.6108 * exp((17.27 * temp) / (temp + 237.3));
    return svp - svp * (humidity / 100.0);
}

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

// Tambahkan sufiks status (mis. " SOILERR") tanpa overflow.
static void statusAdd(char* buf, size_t n, const char* suffix) {
    size_t len = strlen(buf);
    size_t add = strlen(suffix);
    if (len + add + 1 < n) strcat(buf, suffix);
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
    // WIB (UTC+7). Tiga server NTP: kalau satu tidak terjangkau, client
    // otomatis pindah ke server berikutnya (NTP = titik kegagalan tunggal).
    // ts di sensor_logs jadi epoch asli agar waktu dashboard akurat.
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
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
    IrrigPhase prevIrrigPhase = IRRIG_IDLE;

    // Fail-safe I2C nyangkut: subscribe controlTask ke Task WDT. WDT di-init
    // eksplisit di setup() (framework arduino-esp32 2.0.17 / ESP-IDF 4.4 TIDAK
    // memanggil esp_task_wdt_init() sendiri -> subscribe tanpa init = gagal
    // diam-diam dan fail-safe ini mati).
    // readTemperature()/readHumidity() blocking tanpa timeout I2C -> task spin > 5 s ->
    // panic -> reboot -> initActuators() mematikan pompa yang masih nyala.
    // Hanya controlTask: netTask sengaja blocking (join WiFi s/d 15 s, TLS) dan
    // akan memicu reset palsu. Return dicek — fail-safe yang diam-diam gagal
    // subscribe sama dengan tidak ada.
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        Serial.println("[wdt] GAGAL subscribe controlTask - fail-safe I2C tidak aktif");
    }

    for (;;) {
        float soilRaw = analogRead(SOIL_PIN);
        sensorData = readSensors(soilRaw);
        // Filter EMA suhu/RH (input VPD) & soil agar VPD dan keputusan irigasi stabil.
        // Suhu/RH wajib lewat guard valid: readSensors bisa mengembalikan NaN dan
        // NaN yang masuk EMA merusak nilainya secara permanen. Soil tidak - probe
        // soil kanal analog terpisah yang tetap terbaca walau SHT31 mati.
        if (sensorData.valid) {
            sensorData.temperature = emaUpdate(tempFilter, sensorData.temperature);
            sensorData.humidity    = emaUpdate(humFilter, sensorData.humidity);
        }
        // Soil: HANYA nilai valid yang masuk filter. Probe dicabut -> raw
        // mengambang -> 0 %/100 % palsu yang akan meracuni filter dan membuat
        // irigasi nyala/mati diam-diam. Kalau tidak valid, pakai nilai sah
        // terakhir (soilFilter.value) supaya keputusan tidak berubah liar.
        if (sensorData.soilValid) {
            sensorData.soilMoisture = emaUpdate(soilFilter, sensorData.soilMoisture);
        } else if (soilFilter.seeded) {
            sensorData.soilMoisture = soilFilter.value;   // nilai sah terakhir
        } else {
            // Belum pernah ada sampel sah: pakai kondisi NETRAL (50 %), bukan
            // 0 % kering palsu (bikin fuzzy tambah misting tanpa alasan).
            sensorData.soilMoisture = 50.0f;
        }

        // TDS juga difilter, karena inilah kanal paling bising: pompa fert
        // berdenyut 2 s on / 1 s off sementara loop baca tiap 2 s, jadi dua
        // iterasi berurutan menangkap kondisi adukan yang berbeda. Diuji dengan
        // konsentrasi naik 400->1060 ppm dan noise denyut: pembacaan mentah
        // melewati gate saat konsentrasi asli baru 820 ppm (fase FERT berhenti
        // terlalu awal, underpupuk), yang terfilter baru di 1060 ppm. Timeout
        // 120 detik tetap menjadi pengaman atas (filter menambah ~8 detik).
        // Reset tiap siklus masuk FERT hanya pengaman tambahan: setelah fase FILL
        // 30 detik (15 iterasi) residual filter tinggal ~403 ppm, jadi nilainya
        // sudah hampir habis - yang diluted reset adalah FILL yang dipendekkan
        // atau siklus yang dibatalkan di tengah FERT.
        IrrigPhase irrigPhase = irrigationPhase();
        if (irrigPhase == IRRIG_FERT && prevIrrigPhase != IRRIG_FERT) emaReset(tdsFilter);
        prevIrrigPhase = irrigPhase;
        // TDS: hanya nilai sah yang masuk filter (probe dicabut -> jangan
        // meracuni filter dengan angka garbage).
        bool tdsValid = false;
        float tdsSample = readTDS(sensorData.temperature, &tdsValid);
        if (tdsValid) tdsPpm = emaUpdate(tdsFilter, tdsSample);

        if (sensorData.valid) {
            vpdValue = calcVpd(sensorData.temperature, sensorData.humidity);
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
            // Status eksplisit supaya operator/dashboard tahu regulate sudah berhenti
            // (bukan "Normal" basi dari iterasi valid terakhir).
            systemStatus = "Sensor Error";
        }

        // Log kegagalan sensor (dibatasi 1x per 5 s). Kegagalan harus terlihat
        // di port Serial, bukan hanya diam di RTDB.
        if ((!sensorData.valid || !sensorData.soilValid || !tdsValid) &&
            millis() - lastFailPrint > 5000) {
            lastFailPrint = millis();
            if (!sensorData.valid) {
                Serial.println("[!] Sensor SHT31 tidak terhubung - cek wiring");
                Serial.printf("[!] Sensor error: %s\n", getSensorError());
            }
            if (!sensorData.soilValid) {
                Serial.printf("[!] Probe tanah di luar rentang (raw %.0f) - irigasi ditahan\n",
                              soilRaw);
            }
            if (!tdsValid) {
                Serial.println("[!] TDS tidak terbaca sah - dosis pupuk dilewati");
            }
        }

        // Fail-safe: tanpa feedback sensor tidak ada regulate — aktuator dipatikan
        // semuanya (override manual ikut, karena misting/irigasi tidak punya
        // timeout sendiri) dan siklus irigasi yang berjalan dihentikan. Tanpa ini
        // nilai fuzzy terakhir dibekukan selamanya, mis. SHT31 dicabut di tengah
        // siklus -> misting/irigasi tetap nyala tanpa batas.
        bool mist = sensorData.valid && fuzzyOut.mistOn;
        // Soil tidak valid -> keputusan irigasi fuzzy TIDAK dipakai (fail-safe:
        // jangan siram/jangan mati diam-diam dari angka sensor rusak).
        bool irigasi = sensorData.valid && sensorData.soilValid && fuzzyOut.irigasiOn;
        bool blower = sensorData.valid && fuzzyOut.blowerOn;
        float blowerLevel = sensorData.valid ? fuzzyOut.blowerLevel : 0.0f;

        // Setpoint VPD fase = KOREKSI terhadap keluaran fuzzy, bukan pengganti.
        // Fuzzy tetap menentukan apa yang dijalankan; setpoint hanya mendorong
        // satu arah supaya tidak ada kondisi "status Kritis tapi tidak ada aksi".
        //   VPD > target+band -> kebijakan MISTING: misting dinyalakan/diperahankan,
        //                       blower yang diminta fuzzy TIDAK dimatikan.
        //   VPD < target-band -> dorong blower PROPORSIONAL (bukan duty 100 %
        //                       mendadak seperti sebelumnya); misting yang diminta
        //                       fuzzy TIDAK dimatikan.
        //   dalam band        -> fuzzy menentukan sepenuhnya, tanpa override.
        if (sensorData.valid && phaseActive()) {
            float target = phaseVpdTarget();
            if (vpdValue > target + VPD_BAND_KPA) {
                mist = true;
            } else if (vpdValue < target - VPD_BAND_KPA) {
                float deficit = (target - VPD_BAND_KPA) - vpdValue;   // kPa di bawah band
                float boost = constrain(deficit / VPD_BAND_KPA, 0.0f, 1.0f) * VPD_BOOST_MAX;
                blower = true;
                if (boost > blowerLevel) blowerLevel = boost;
            }
        }

        // Override manual (tris-tata per aktuator) — manual tetap menang.
        if (sensorData.valid) controlApply(&mist, &irigasi, &blower, &blowerLevel);
        bool manual = controlIsManual();
        setActuators(mist, blower, blowerLevel);

        // Irigasi 4-pompa: sensor mati = batalkan siklus; selain itu serah ke
        // fuzzy (edge-trigger di irrigationUpdate — cycle sekali per permintaan).
        // Sakelar manual yang dimatikan TIDAK lagi membatalkan siklus: override
        // manual per aktuator, jadi mematikan sakelar berarti kembali ke fuzzy.
        if (!sensorData.valid) {
            irrigationAbort();
        } else {
            irrigationUpdate(irigasi, tdsPpm, tdsValid);
        }
        bool irigasiRunning = irrigationRunning();

        // Serial: SELALU cetak (walau sensor invalid) supaya kegagalan terlihat
        // di log port, bukan hanya diam di RTDB.
        Serial.printf("T:%.1fC H:%.1f%% Soil:%.1f%%%s VPD:%.2fkPa ETc:%.1fmm TDS:%.0fppm%s [%s]",
                      sensorData.valid ? sensorData.temperature : 0.0f,
                      sensorData.valid ? sensorData.humidity : 0.0f,
                      sensorData.soilMoisture, sensorData.soilValid ? "" : "!",
                      vpdValue, etcValue, tdsPpm, tdsValid ? "" : "!", systemStatus);
        Serial.printf(" Mist:%s Irig:%s Blower:%s(lv%.0f%%) Fase:%s VPDtgt:%.2f\n",
                      mist ? "ON" : "OFF", irigasiRunning ? "RUN" : "OFF",
                      blower ? "ON" : "OFF", blowerLevel * 100.0,
                      phaseGet().name, phaseVpdTarget());

        if (sensorData.valid) {
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
            updateLcd(sensorData.valid ? sensorData.temperature : 0.0f,
                      sensorData.valid ? sensorData.humidity : 0.0f,
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
        s.manual = manual;
        s.mist = mist;
        s.blower = blower;
        s.irigasiRunning = irigasiRunning;
        s.blowerLevel = blowerLevel;   // keluaran fuzzy yang jadi perintah PWM
        // status_sistem = status fuzzy + penanda aktuator hidup + penanda
        // kegagalan sensor/jam. Dashboard membaca string ini (pill + tabel),
        // jadi penanda kegagalan harus ikut di sini.
        snprintf(s.status, sizeof(s.status), "%s", systemStatus);
        if (mist) statusAdd(s.status, sizeof(s.status), " MISTING");
        if (blower) statusAdd(s.status, sizeof(s.status), " BLOWER");
        if (irigasiRunning) statusAdd(s.status, sizeof(s.status), " IRIGASI");
        if (!sensorData.soilValid) statusAdd(s.status, sizeof(s.status), " SOILERR");
        if (!tdsValid) statusAdd(s.status, sizeof(s.status), " TDSERR");
        if (time(nullptr) < 100000) statusAdd(s.status, sizeof(s.status), " NOSYNC");
        s.phase = phaseGet();
        snapshotPublish(s);

        esp_task_wdt_reset();
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
            // Push telemetry SELALU (bukan hanya saat sensor valid): status
            // "Sensor Error" yang sudah disiapkan harus sampai ke RTDB, kalau
            // tidak dashboard tidak bisa membedakan "sensor mati" dari
            // "device mati".
            if (now - lastPush >= DB_PUSH_MS) {
                lastPush = now;
                String body;
                bool ok = pushSensorLog(s.temperature, s.humidity, s.soilMoisture,
                                        s.vpd, s.etc, s.tds, s.blowerLevel, s.mist,
                                        s.irigasiRunning, s.status, s.manual, s.phase, &body);
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

    // Task WDT: framework arduino-esp32 2.0.17 (ESP-IDF 4.4) TIDAK pernah
    // memanggil esp_task_wdt_init() sendiri (CONFIG_ESP_TASK_WDT_INIT tidak
    // ada di sdkconfig), jadi subscribe controlTask DI GAGAL diam-diam
    // (ESP_ERR_INVALID_STATE) dan jaring pengaman I2C nyangkut TIDAK aktif
    // (device beku, aktuator tetap nyala). Inisialisasi eksplisit di sini
    // (timeout 5 s, panic -> reboot; initActuators() mematikan pompa).
    esp_err_t wdtErr = esp_task_wdt_init(WDT_TIMEOUT_S, true);
    if (wdtErr == ESP_OK) {
        Serial.printf("[wdt] Task WDT aktif (timeout %d s, panic)\n", WDT_TIMEOUT_S);
    } else {
        Serial.printf("[wdt] GAGAL init Task WDT: %s\n", esp_err_to_name(wdtErr));
    }

    initLcd();
    initSdLogger();
    initSensor();
    emaInit(tempFilter, EMA_ALPHA_TEMP);
    emaInit(humFilter, EMA_ALPHA_HUM);
    emaInit(soilFilter, EMA_ALPHA_SOIL);
    emaInit(tdsFilter, EMA_ALPHA_TDS);
    phaseInit();
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
