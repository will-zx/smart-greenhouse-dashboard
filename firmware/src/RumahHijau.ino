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
#include "sd_logger.h"
#include "firebase_db.h"
#include "lcd_display.h"
#include "wifi_manager.h"

// ==== Arsitektur 2 task (FreeRTOS) ====
// controlTask (core 1): sensor -> VPD/ETc -> fuzzy -> aktuator -> irigasi -> LCD -> SD.
// netTask (core 0): WiFi, token, push RTDB, poll perintah, backlog SD.
// Tujuan: I/O jaringan (TLS bisa beberapa detik, WiFi join s/d WIFI_TIMEOUT)
// tidak lagi menahan jalur aktuator -> siklus irigasi (pompa DIST) tidak
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
    float temperature, humidity, soilMoisture, soil2, soil3, vpd, etc, tds, tmean;
    bool  manual, mist, blower, irigasiRunning;
    float blowerLevel;  // 0..1, tingkat blower yang benar-benar dijalankan (PWM)
    char  status[96];   // label aksi (buildStatus) + penanda SOILERR/TDSERR/...
    uint8_t soilOk;     // bitmask probe tanah valid (0 = semua mati)
    uint8_t soilCount;  // jumlah probe tanah valid
    PhaseState phase;   // setelan fase + VPD target (untuk telemetri DB)
    OverrideState ov;   // status override manual per aktuator (sakelar dashboard)
    IrrigStatus irig;   // dosis/durasi/anggaran irigasi (telemetri DB)
};

static Snapshot gSnap;
static SemaphoreHandle_t snapMux = nullptr;

// Filter EMA sinyal sensor (lihat filter.h). Alfa dari config.h.
static EmaFilter tempFilter, humFilter, tdsFilter;
static EmaFilter soilFilter[SOIL_COUNT];   // satu EMA per probe tanah

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

// Label status yang dibaca operator: AKSI yang sedang berjalan, bukan kata
// kondisi yang ditempeli nama aktuator ("Warning MISTING" aneh dibaca dan
// tidak memberi tahu apa yang sedang dilakukan alat). Urutan prioritas:
// irigasi (paling banyak memindahkan air) -> misting -> blower.
// Tanpa aktuator nyala, pakai kata kondisi fuzzy apa adanya (Normal/Monitor/
// Warning/Kritis) supaya tidak ada kode mati di fuzzy.cpp.
static void buildStatus(char* buf, size_t n, const char* cond,
                        bool irig, bool mist, bool blower) {
    const char* act[3];
    int k = 0;
    if (irig)   act[k++] = "Irigasi";
    if (mist)   act[k++] = "Misting";
    if (blower) act[k++] = "Blower";
    if (k == 0)      snprintf(buf, n, "%s", cond);
    else if (k == 1) snprintf(buf, n, "%s ON", act[0]);
    else if (k == 2) snprintf(buf, n, "%s + %s ON", act[0], act[1]);
    else             snprintf(buf, n, "%s + %s + %s ON", act[0], act[1], act[2]);
}

// Alasan reset perangkat (WDT panic / brownout / restart manual) supaya
// reboot tak terduga kelihatan di log boot.
static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_SW:        return "software (restart perintah / restart manual)";
        case ESP_RST_PANIC:     return "panic (exception)";
        case ESP_RST_INT_WDT:   return "interrupt WDT";
        case ESP_RST_TASK_WDT:  return "TASK WDT (panic-reboot Task Watchdog)";
        case ESP_RST_WDT:       return "WDT lain";
        case ESP_RST_BROWNOUT:  return "brownout (jatuh tegangan PSU)";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_EXT:       return "ext pin";
        default:                return "tidak diketahui";
    }
}

// ==== WiFi ====
static unsigned long lastReconnect = 0;

#include "wifi_manager.h"

void connectWiFi() {
    // Semua logika koneksi (termasuk fallback ke AP setup) dipindahkan ke WiFiManager.
    wifiManager.begin();
    // catatan: wifiManager sudah mengatur lastReconnect bila berhasil.
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
    unsigned long lastGatePrint = 0;
    // Bitmask probe iterasi sebelumnya. 0xFF = belum pernah, supaya baris
    // status probe pertama tetap tercetak setelah boot.
    uint8_t lastSoilOk = 0xFF;
    TickType_t lastWake = xTaskGetTickCount();

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
        sensorData = readSensors();          // T/RH + tiga probe tanah (SOIL_PINS)
        // Filter EMA suhu/RH (input VPD) & soil agar VPD dan keputusan irigasi stabil.
        // Suhu/RH wajib lewat guard valid: readSensors bisa mengembalikan NaN dan
        // NaN yang masuk EMA merusak nilainya secara permanen. Soil tidak - probe
        // soil kanal analog terpisah yang tetap terbaca walau SHT31 mati.
        if (sensorData.valid) {
            sensorData.temperature = emaUpdate(tempFilter, sensorData.temperature);
            sensorData.humidity    = emaUpdate(humFilter, sensorData.humidity);
        }
        // Soil: HANYA probe valid yang masuk filter dan rata-rata. Probe dicabut
        // -> raw mengambang -> 0 %/100 % palsu yang akan meracuni filter dan
        // membuat irigasi nyala/mati diam-diam. Probe yang mati tidak menarik
        // rata-rata; kalau semuanya mati, pakai nilai sah terakhir.
        float soilSum = 0.0f;
        int   soilN = 0;
        for (int i = 0; i < SOIL_COUNT; i++) {
            if (isnan(sensorData.soilPct[i])) continue;
            sensorData.soilPct[i] = emaUpdate(soilFilter[i], sensorData.soilPct[i]);
            soilSum += sensorData.soilPct[i];
            soilN++;
        }
        if (soilN > 0) {
            sensorData.soilMoisture = soilSum / (float)soilN;
        } else {
            float lastSum = 0.0f;
            int   lastN = 0;
            for (int i = 0; i < SOIL_COUNT; i++) {
                if (soilFilter[i].seeded) { lastSum += soilFilter[i].value; lastN++; }
            }
            // Belum pernah ada sampel sah: pakai kondisi NETRAL (50 %), bukan
            // 0 % kering palsu (bikin fuzzy tambah misting tanpa alasan).
            sensorData.soilMoisture = (lastN > 0) ? (lastSum / (float)lastN) : 50.0f;
        }

        // Jejak perubahan status probe. Dicetak HANYA saat bitmask berubah
        // (bukan tiap 2 s) supaya serial tidak banjir, tapi operator tetap tahu
        // kapan probe tercabut / tersambung lagi — kejadian yang di dashboard
        // hanya terlihat sebagai hilangnya soil2/soil3.
        if (sensorData.soilOk != lastSoilOk) {
            lastSoilOk = sensorData.soilOk;
            char okList[24] = "";
            char badList[80] = "";
            for (uint8_t ch = 0; ch < SOIL_COUNT; ch++) {
                char item[24];
                if (sensorData.soilOk & (uint8_t)(1u << ch)) {
                    snprintf(item, sizeof(item), "%s%u", okList[0] ? "," : "", ch + 1);
                    strncat(okList, item, sizeof(okList) - strlen(okList) - 1);
                } else {
                    snprintf(item, sizeof(item), "%s%u (raw %.0f)",
                             badList[0] ? ", " : "", ch + 1, sensorData.soilRaw[ch]);
                    strncat(badList, item, sizeof(badList) - strlen(badList) - 1);
                }
            }
            if (!sensorData.soilOk) {
                Serial.printf("[soil] semua probe tidak terbaca: %s\n", badList);
            } else if (!badList[0]) {
                Serial.printf("[soil] semua probe OK (%s)\n", okList);
            } else {
                Serial.printf("[soil] probe %s OK · probe %s tidak terbaca\n",
                              okList, badList);
            }
        }

        // TDS dipakai sebagai PEMERIKSA di fase CHECK: dosis stok A/B jalan
        // selama durasi tetap (lingkaran terbuka), jadi TDS memverifikasi
        // dugaan itu, bukan mengendalikannya. Nilainya difilter EMA supaya
        // angka yang tampil dan yang dinilai sama-sama stabil.
        bool tdsValid = false;
        float tdsSample = readTDS(sensorData.temperature, &tdsValid);
        if (tdsValid) tdsPpm = emaUpdate(tdsFilter, tdsSample);

        if (sensorData.valid) {
            // Tmean (rata-rata sliding 3 jam) dikumpulkan dari suhu yang sudah
            // difilter EMA; hanya sampel valid yang masuk jendela.
            irrigationFeedTemp(sensorData.temperature);
            vpdValue = calcVpd(sensorData.temperature, sensorData.humidity);
            // ETc = Kc(fase) * p * (0,46*Tmean + 8) — pendekatan praktis
            // Blaney-Criddle, bukan ETo standar FAO-56.
            etcValue = calculateEtc();
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
                Serial.printf("[!] Probe tanah di luar rentang (raw %.0f/%.0f/%.0f) - irigasi fuzzy DITAHAN (override manual tetap berlaku)\n",
                              sensorData.soilRaw[0], sensorData.soilRaw[1], sensorData.soilRaw[2]);
            }
            if (!tdsValid) {
                Serial.println("[!] TDS tidak terbaca sah - TDS hanya telemetri");
            }
        }

        // Log irigasi otomatis yang ditahan gate kalibrasi (dibatasi 1x/30 s).
        // Tanpa ini operator tidak tahu kenapa fuzzy minta irigasi tapi pompa
        // tidak pernah jalan.
        if (!IRRIG_AUTO_ENABLED && sensorData.valid && sensorData.soilValid &&
            fuzzyOut.irigasiOn && millis() - lastGatePrint > 30000) {
            lastGatePrint = millis();
            Serial.println("[irigasi] irigasi otomatis DITAHAN: debit pompa & luas media belum dikalibrasi (IRRIG_AUTO_ENABLED=0) - manual tetap bisa");
        }

        // Fail-safe: tanpa feedback sensor tidak ada regulate — aktuator dipatikan
        // semuanya (override manual ikut, karena misting/irigasi tidak punya
        // timeout sendiri) dan siklus irigasi yang berjalan dihentikan. Tanpa ini
        // nilai fuzzy terakhir dibekukan selamanya, mis. SHT31 dicabut di tengah
        // siklus -> misting/irigasi tetap nyala tanpa batas.
        bool mist = sensorData.valid && fuzzyOut.mistOn;
        // Keputusan irigasi fuzzy HANYA dipakai bila:
        //  - sensor SHT31 valid (butuh T/RH untuk ETc),
        //  - probe tanah valid (kalau tidak, angka 0 %/100 % palsu jadi dasar
        //    keputusan irigasi),
        //  - irigasi otomatis diizinkan (debit pompa & luas media belum
        //    dikalibrasi -> durasi pompa bisa 10-100x kebutuhan tanaman).
        // Override manual operator TETAP menang: itu perintah eksplisit.
        bool irigasi = IRRIG_AUTO_ENABLED && sensorData.valid && sensorData.soilValid
                       && fuzzyOut.irigasiOn;
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
        OverrideState ov = controlOverrides();
        setActuators(mist, blower, blowerLevel);

        // Irigasi (siklus FILL -> FERT -> SETTLE -> CHECK -> DIST): sensor
        // SHT31 mati = batalkan siklus (fail-safe penuh: tanpa T/RH tidak ada
        // ETc/VPD yang sah, override manual pun tidak boleh mengecualikan
        // device dari fail-safe ini); selain itu serah ke irrigationUpdate()
        // yang memutuskan mulai/tidak siklus dari trigger + jeda minimum +
        // anggaran harian. TDS dikirim supaya fase CHECK bisa menilainya.
        // Sakelar manual yang dimatikan TIDAK lagi membatalkan siklus: override
        // manual per aktuator, jadi mematikan sakelar berarti kembali ke fuzzy.
        if (!sensorData.valid) {
            irrigationAbort();
        } else if (ov.irig == 0) {
            // "Paksa mati" dari dashboard (mode off): siklus yang sedang
            // berjalan harus benar-benar berhenti, bukan cuma dicegah memulai
            // yang baru. Diperiksa TIAP iterasi supaya abort yang dikirim
            // netTask tidak bisa dilanjutkan lagi oleh state machine di
            // iterasi berikutnya.
            irrigationAbort();
        } else {
            // manual = override operator memaksa irigasi nyala (ov.irig == 1).
            // Perintah eksplisit itu boleh menembus jeda & anggaran harian,
            // tapi hanya satu siklus per permintaan manual.
            irrigationUpdate(irigasi, ov.irig == 1, tdsPpm, tdsValid);
        }
        bool irigasiRunning = irrigationRunning();
        IrrigStatus irig = irrigationStatus();

        // Nilai yang dilaporkan (serial, LCD, RTDB). Sensor yang tidak
        // terbaca dilaporkan NaN -> field-nya dibuang saat push (dashboard
        // menampilkan "-" dan grafik tidak memplot titik palsu). Angka 0 atau
        // nilai basi TIDAK boleh dikirim sebagai pengganti.
        float rTemp = sensorData.valid ? sensorData.temperature : NAN;
        float rHum  = sensorData.valid ? sensorData.humidity : NAN;
        float rSoil = sensorData.soilValid ? sensorData.soilMoisture : NAN;
        // Probe 2/3 dilaporkan terpisah supaya pot yang mengering lebih dulu
        // kelihatan. NAN = probe itu tidak masuk akal siklus ini (field dibuang
        // saat push, dashboard menampilkan "-").
        float rSoil2 = sensorData.soilPct[1];
        float rSoil3 = sensorData.soilPct[2];
        float rVpd  = sensorData.valid ? vpdValue : NAN;
        float rEtc  = sensorData.valid ? etcValue : NAN;
        float rTds  = tdsValid ? tdsPpm : NAN;
        float rTmean = irrigationTmean();

        // Label status dipakai bersama oleh serial, CSV SD, LCD, dan RTDB —
        // satu definisi supaya yang dibaca operator sama di semua tempat.
        char statusTxt[96];
        buildStatus(statusTxt, sizeof(statusTxt), systemStatus,
                    irigasiRunning, mist, blower);

        // Serial: SELALU cetak (walau sensor invalid) supaya kegagalan terlihat
        // di log port, bukan hanya diam di RTDB.
        Serial.printf("T:%.1fC H:%.1f%% Soil:%.1f%% VPD:%.2fkPa ETc:%.1fmm TDS:%.0fppm Tmean:%.1f [%s]",
                      rTemp, rHum, rSoil, rVpd, rEtc, rTds, rTmean, statusTxt);
        Serial.printf(" Mist:%s Irig:%s Blower:%s(lv%.0f%%) Fase:%s Kc:%.2f VPDtgt:%.2f",
                      mist ? "ON" : "OFF", irigasiRunning ? "RUN" : "OFF",
                      blower ? "ON" : "OFF", blowerLevel * 100.0,
                      phaseGet().name, phaseKc(), phaseVpdTarget());
        // Status override manual: '-' = ikut fuzzy, '+' = paksa nyala,
        // '0' = paksa mati (identik dengan mist_ov/irig_ov/blower_ov di RTDB).
        Serial.printf(" OV[m%c i%c b%c] SoilRaw:%.0f/%.0f/%.0f\n",
                      ov.mist < 0 ? '-' : (ov.mist ? '+' : '0'),
                      ov.irig < 0 ? '-' : (ov.irig ? '+' : '0'),
                      ov.blower < 0 ? '-' : (ov.blower ? '+' : '0'),
                      sensorData.soilRaw[0], sensorData.soilRaw[1], sensorData.soilRaw[2]);

        if (sensorData.valid) {
            // Backup lokal: seluruh pembacaan valid dicatat ke MicroSD.
            time_t ts = time(nullptr);
            if (ts >= 100000) {
                char line[128];
                snprintf(line, sizeof(line), "%ld,%.1f,%.1f,%.1f,%.2f,%.1f,%s,%d",
                         (long)ts, sensorData.temperature, sensorData.humidity,
                         sensorData.soilMoisture, vpdValue, etcValue,
                         statusTxt, (int)manual);
                sdLogRecord(line);
            }
        }

        unsigned long now = millis();
        if (now - lastLcd >= LCD_INTERVAL) {
            lastLcd = now;
            updateLcd(rTemp, rHum, rSoil, rVpd, rEtc, rTds);
        }

        // Publikasikan ke netTask. status_sistem lengkap dengan suffix aktuator —
        // itu yang dipakai dashboard untuk posisi switch & pill.
        Snapshot s = {};
        s.temperature = rTemp;
        s.humidity = rHum;
        s.soilMoisture = rSoil;
        s.soil2 = rSoil2;
        s.soil3 = rSoil3;
        s.soilOk = sensorData.soilOk;
        s.soilCount = sensorData.soilCount;
        s.vpd = rVpd;
        s.etc = rEtc;
        s.tds = rTds;
        s.tmean = rTmean;
        s.manual = manual;
        s.mist = mist;
        s.blower = blower;
        s.irigasiRunning = irigasiRunning;
        s.blowerLevel = blowerLevel;   // keluaran fuzzy yang jadi perintah PWM
        s.ov = ov;
        s.irig = irig;
        // status_sistem = label aksi (buildStatus) + penanda kegagalan sensor/
        // jam. Dashboard membaca string ini (pill + tabel + ikon error), jadi
        // penanda kegagalan harus ikut di sini; sakelar aktuator sendiri dibaca
        // dari field boolean/angka, bukan dari teks ini.
        snprintf(s.status, sizeof(s.status), "%s", statusTxt);
        if (!sensorData.soilValid) statusAdd(s.status, sizeof(s.status), " SOILERR");
        if (!tdsValid) statusAdd(s.status, sizeof(s.status), " TDSERR");
        if (time(nullptr) < 100000) statusAdd(s.status, sizeof(s.status), " NOSYNC");
        if (!IRRIG_AUTO_ENABLED) statusAdd(s.status, sizeof(s.status), " IRIGOFF");
        // LCD ikut menampilkan penanda error (sekarang bisa, karena teksnya
        // bergeser; dulu kepotong di kolom 20).
        lcdScrollSetStatus(s.status);
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

        // AP setup butuh handleClient() dan timeout; tanpa ini form provisioning
        // tidak pernah dilayani dan AP tidak pernah mati.
        wifiManager.handle();

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

        if (WiFi.status() == WL_CONNECTED && now - lastCmd >= DB_POLL_MS) {
            lastCmd = now;
            pollCommands();
        }

        // Push telemetry SELALU (bukan hanya saat sensor valid): status
        // "Sensor Error" yang sudah disiapkan harus sampai ke RTDB, kalau
        // tidak dashboard tidak bisa membedakan "sensor mati" dari
        // "device mati". Saat offline, body tetap dibuat lalu masuk backlog SD.
        if (now - lastPush >= DB_PUSH_MS) {
            lastPush = now;
            String body;
            bool ok = pushSensorLog(s.temperature, s.humidity, s.soilMoisture,
                                    s.soil2, s.soil3, s.soilOk, s.soilCount,
                                    s.vpd, s.etc, s.tds, s.blowerLevel, s.mist,
                                    s.irigasiRunning, s.status, s.manual, s.phase,
                                    s.ov, s.tmean, s.irig, &body);
            if (ok) {
                sdFlushBacklog();   // pulih: kirim antrean yang tertunda
            } else if (body.length() > 0) {
                sdQueueBacklog(body.c_str());  // gagal/offline: antre ke MicroSD
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
    // Jejak boot: alasan reset (panic-reboot WDT, brownout, restart manual)
    // supaya reboot tak terduga bisa dibedakan dari restart yang disengaja.
    Serial.printf("[boot] alasan reset: %s | uptime %lu ms\n",
                  resetReasonName(esp_reset_reason()), (unsigned long)millis());

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

    i2cBusInit();     // mutex bus I2C dipakai SHT31 + LCD (dua task)
    initLcd();
    lcdScrollStart(); // task marquee baris status (core 0, prio 1)
    initSdLogger();
    initSensor();
    emaInit(tempFilter, EMA_ALPHA_TEMP);
    emaInit(humFilter, EMA_ALPHA_HUM);
    for (int i = 0; i < SOIL_COUNT; i++) emaInit(soilFilter[i], EMA_ALPHA_SOIL);
    emaInit(tdsFilter, EMA_ALPHA_TDS);
    phaseInit();
    // Jejak boot: fase + Kc aktif + status irigasi otomatis (gate kalibrasi).
    Serial.printf("[boot] fase: %s (Kc %.2f) | irigasi otomatis %s\n",
                  phaseGet().name, phaseKc(),
                  IRRIG_AUTO_ENABLED ? "AKTIF"
                                      : "MATI (debit pompa & luas media belum dikalibrasi)");
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
