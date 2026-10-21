#include "firebase_db.h"
#include "config.h"
#include "control.h"
#include "phase.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

static WiFiClientSecure client;
static HTTPClient http;

// ==== Kontrak data sensor_logs (RTDB) ====
// Satu record per push (tiap ~20 s) di /zones/{DEVICE_ID}/sensor_logs/{key}.
// Field yang TIDAK boleh diubah namanya tanpa persetujuan antar subsistem.
// Field angka yang tidak terbaca TIDAK dikirim (dashboard menampilkan "-").
//
//   temperature      °C      suhu udara (SHT31)
//   humidity         %       kelembapan relatif udara (SHT31)
//   soil             %       kelembapan tanah (probe capacitive)
//   vpd              kPa     defisit tekanan uap (Tetens, dari T & RH)
//   etc              mm/hari kebutuhan air tanaman (Blaney-Criddle x Kc fase)
//   et0              mm/hari ALIAS LAMA dari "etc" (deprecated, hapus nanti)
//   tmean_c          °C      Tmean sliding 3 jam yang dipakai menghitung ETc
//   kc_aktif         -       Kc fase berjalan (Tabel 2.1 proposal)
//   tds              ppm     TDS larutan (TELEMETRI SAJA, bukan alat dosis)
//   blower_level     0..1    duty blower yang dijalankan (PWM 1 kHz)
//   misting          bool    pompa misting hidup
//   irigasi          bool    siklus irigasi berjalan
//   status_sistem    string  status fuzzy + penanda (MISTING/BLOWER/IRIGASI/
//                            SOILERR/TDSERR/NOSYNC/IRIGOFF)
//   manual_mode      bool    ada aktuator yang di-override manual
//   mist_ov          string  override misting: "auto" | "manual" | "forced"
//   irig_ov          string  override irigasi: "auto" | "manual" | "forced"
//   blower_ov        string  override blower:  "auto" | "manual" | "forced"
//   fase             string  nama fase pertumbuhan
//   vpd_target       kPa     target VPD fase
//   fase_aktif       bool    mode setpoint aktif
//   fase_durasi_s    s       durasi fase
//   fase_elapsed_s   s       lama fase berjalan
//   fase_sisa_s      s       sisa durasi fase
//   irigasi_dosis_ml     mL     dosis air per siklus (dari ETc)
//   irigasi_durasi_ms   ms     durasi pompa DIST untuk siklus itu
//   irigasi_anggaran_ml mL     anggaran harian (ETc x luas area x 0,1)
//   irigasi_terpakai_ml mL     volume yang sudah dialirkan hari ini
//   irigasi_jeda_menit  menit  jeda minimum antar siklus
//   irigasi_alasan_tahan string alasan permintaan ditahan ("" = tidak ditahan)
//   time_synced       bool    jam perangkat sudah sinkron NTP
//   ts                s       epoch waktu pengiriman (hanya bila sinkron)
//   device            string  id unit
//
// ==== TLS (P2-12) — MASIH PROTOTYPING ====
// Semua koneksi memakai client.setInsecure() -> verifikasi CA dimatikan.
// Risikonya nyata: siapa pun yang bisa melakukan MITM di access point bisa
// mencuri password akun perangkat + idToken dan mengirim perintah aktuator.
//
// Memperbaikinya (client.setCACert(<root CA Google>)) harus dilakukan sebagai
// langkah TERPISAH dan diuji di device: boot -> WiFi tersambung -> login
// perangkat OK -> push sensor_logs OK. Jangan patch buta: CA yang salah
// membuat semua koneksi gagal diam-diam (device Offline tanpa jejak).
// Lihat catatan "Hardware gotchas" di AGENTS.md.

// Token akun perangkat. Valid 1 jam; di-refresh sebelum itu agar tidak
// expired di tengah push.
static String idToken;
static unsigned long tokenExpiryMs = 0;

// Login perangkat via Firebase Auth REST (Identity Toolkit).
// Idempotent: skip kalau masih ada > TOKEN_REFRESH_MS tersisa.
static bool ensureToken() {
    if (idToken.length() > 0 && millis() < tokenExpiryMs) return true;
    if (WiFi.status() != WL_CONNECTED) return false;

    client.setInsecure();
    String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=")
                 + FIREBASE_API_KEY;
    // Body dibangun dengan ArduinoJson: password dengan karakter khusus
    // (" atau \) bikin JSON rusak kalau dirakit manual dengan string.
    JsonDocument req;
    req["email"] = DEVICE_EMAIL;
    req["password"] = DEVICE_PASSWORD;
    req["returnSecureToken"] = true;
    String body;
    serializeJson(req, body);

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String payload = http.getString();
    http.end();

    if (code != 200) {
        Serial.printf("[fb] login gagal: HTTP %d %s\n", code, payload.c_str());
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, payload) || doc["idToken"].isNull()) {
        Serial.println("[fb] login gagal: idToken tidak ada di respons");
        return false;
    }
    idToken = doc["idToken"].as<String>();
    // expiresIn dalam detik; kurangi margin supaya tidak expired saat dipakai.
    long expiresIn = doc["expiresIn"] | 3600L;
    tokenExpiryMs = millis() + (unsigned long)(expiresIn * 1000L) - TOKEN_REFRESH_MS;
    Serial.printf("[fb] login perangkat OK (%s), token %d menit\n",
                  DEVICE_EMAIL, (int)(expiresIn / 60));
    return true;
}

// Paksa login ulang (dipakai saat server menolak dengan 401).
static void invalidateToken() {
    idToken = "";
    tokenExpiryMs = 0;
}

// ==== Cooldown perintah restart ====
// Disimpan di NVS (bukan hanya RAM) supaya reboot berulang-ulang dari admin
// tidak lolos: setiap reboot menghapus RAM, jadi cooldown harus bertahan.
static Preferences prefsCmd;
static bool     restartStateLoaded = false;
static time_t   lastRestartEpoch = 0;      // epoch detik restart terakhir
static unsigned long lastRestartMs = 0;     // fallback kalau jam belum sinkron

static void loadRestartState() {
    if (restartStateLoaded) return;
    restartStateLoaded = true;
    prefsCmd.begin("smartgh_cmd", true);
    lastRestartEpoch = (time_t)prefsCmd.getULong("last_restart", 0);
    prefsCmd.end();
}

static bool restartBlocked() {
    loadRestartState();
    time_t now = time(nullptr);
    if (now >= 100000 && lastRestartEpoch > 0) {
        return (now - lastRestartEpoch) < (time_t)(RESTART_COOLDOWN_MS / 1000UL);
    }
    return lastRestartMs != 0 && (millis() - lastRestartMs) < RESTART_COOLDOWN_MS;
}

static void markRestart() {
    lastRestartMs = millis();
    lastRestartEpoch = time(nullptr);
    prefsCmd.begin("smartgh_cmd", false);
    prefsCmd.putULong("last_restart", (unsigned long)lastRestartEpoch);
    prefsCmd.end();
}

// ponytail: setInsecure(true) melepas verifikasi CA (lihat catatan TLS di
// atas). Ceiling: development/test. Untuk produksi: client.setCACert(<root
// cert google>) — tambah ~2KB flash.
static bool httpPost(const String& path, const String& body) {
    if (!ensureToken()) return false;
    client.setInsecure();
    String url = String(FIREBASE_DB_URL) + path + ".json?auth=" + idToken;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    http.end();
    if (code == 401) invalidateToken();   // token expired/kadaluarsa
    return code >= 200 && code < 300;
}

// NaN/Inf tidak boleh masuk RTDB (diparse sebagai null dan merusak grafik
// dashboard) -> field yang tidak valid simply tidak dikirim. Dashboard
// membaca field yang hilang sebagai "-" dan tidak memplot titik palsu.
static void putNum(JsonDocument& doc, const char* key, float v) {
    if (isnan(v) || isinf(v)) return;
    doc[key] = v;
}

// Status override manual per aktuator -> string untuk dashboard.
//   "auto"   = ikut fuzzy (tidak ada override)
//   "manual" = dipaksa NYALA
//   "forced" = dipaksa MATI
// Angka setara di control.cpp: -1 / +1 / 0. Dashboard WAJIB memakai field ini
// untuk menampilkan sakelar override (bukan status aktuator).
static const char* ovName(int8_t ov) {
    if (ov < 0) return "auto";
    return (ov == 1) ? "manual" : "forced";
}

bool pushSensorLog(float temperature, float humidity, float soil, float vpd, float etc, float tds,
                   float blowerLevel, bool misting, bool irigasi,
                   const char* statusSistem, bool manualMode, const PhaseState& phase,
                   const OverrideState& ov, float tmean, const IrrigStatus& irig,
                   String* bodyOut) {
    if (WiFi.status() != WL_CONNECTED) return false;

    // ts = epoch (NTP). Kalau NTP belum sinkron, record TETAP dikirim (tanda
    // kehidupan device) tapi TANPA field ts — epoch 1970 tidak boleh masuk
    // RTDB karena dashboard mengurutkan/menghitung umurnya dari ts.
    time_t ts = time(nullptr);
    bool timeOk = (ts >= 100000);

    JsonDocument doc;
    putNum(doc, "temperature", temperature);
    putNum(doc, "humidity", humidity);
    putNum(doc, "soil", soil);
    putNum(doc, "vpd", vpd);
    // "etc" = nama final (isi = ETc, sudah x Kc). "et0" disimpan sebagai
    // alias lama supaya dashboard yang belum migrate tidak ikut rusak;
    // hapus "et0" setelah sesi web memakai "etc".
    putNum(doc, "etc", etc);
    putNum(doc, "et0", etc);
    putNum(doc, "tds", tds);
    // Keluaran fuzzy = perintah aktuator yang dijalankan. Dashboard memakai
    // angka ini, bukan menguraikan string status_sistem.
    putNum(doc, "blower_level", blowerLevel);
    doc["misting"] = misting;
    doc["irigasi"] = irigasi;
    doc["status_sistem"] = statusSistem;
    doc["manual_mode"] = manualMode;
    // Status override per aktuator (sakelar dashboard).
    doc["mist_ov"] = ovName(ov.mist);
    doc["irig_ov"] = ovName(ov.irig);
    doc["blower_ov"] = ovName(ov.blower);
    // Telemetri fase & VPD target (di-set admin dari dashboard).
    doc["fase"] = phase.name;
    doc["vpd_target"] = phase.vpdTarget;
    doc["fase_aktif"] = phase.active;
    doc["fase_durasi_s"] = phase.durationSec;
    doc["fase_elapsed_s"] = phase.elapsedSec;
    doc["fase_sisa_s"] = phase.remainingSec;
    // Kc fase aktif (Tabel 2.1 proposal) + Tmean yang dipakai menghitung ETc
    // (pendekatan praktis, bukan ETo standar FAO-56).
    doc["kc_aktif"] = phase.kc;
    putNum(doc, "tmean_c", tmean);
    // Telemetri irigasi: dosis & durasi siklus, anggaran harian, volume
    // terpakai hari ini, dan jeda minimum (lihat irrigation.h).
    putNum(doc, "irigasi_dosis_ml", irig.doseMl);
    putNum(doc, "irigasi_durasi_ms", (float)irig.doseMs);
    putNum(doc, "irigasi_anggaran_ml", irig.budgetMl);
    putNum(doc, "irigasi_terpakai_ml", irig.usedMl);
    putNum(doc, "irigasi_jeda_menit", (float)(irig.minIntervalMs / 60000UL));
    // Alasan permintaan irigasi ditahan (jeda / anggaran / ETc belum ada).
    // String kosong = tidak ada yang ditahan. Dashboard menampilkannya
    // supaya operator tahu kenapa air tidak keluar.
    doc["irigasi_alasan_tahan"] = irig.holdReason ? irig.holdReason : "";
    doc["time_synced"] = timeOk;
    if (timeOk) doc["ts"] = ts;
    doc["device"] = DEVICE_ID;
    String body;
    serializeJson(doc, body);
    if (bodyOut) *bodyOut = body;

    bool ok = httpPost(LOGS_PATH, body);
    if (!ok) Serial.println("[fb] push log gagal (akan diantrekan ke SD)");
    return ok;
}

bool httpPostSensorLogs(const String& body) {
    if (WiFi.status() != WL_CONNECTED) return false;
    return httpPost(LOGS_PATH, body);
}

void pollCommands() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!ensureToken()) return;

    client.setInsecure();
    String url = String(FIREBASE_DB_URL) + "/commands/" + DEVICE_ID + ".json?auth=" + idToken;
    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        if (code == 401) invalidateToken();
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();
    if (payload == "null" || payload == "{}") return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    JsonObject cmds = doc.as<JsonObject>();
    if (err || cmds.size() == 0) return;

    // ponytail: mengurutkan dengan string compare push-ID Firebase (terurut waktu).
    // Ceiling: semua perintah ter-fetch tiap 5s; jika volume perintah besar, pakai
    // orderBy="$value/createdAt"&limitToFirst=1 di query (butuh field sort ekstra).
    // Firebase push-ID terurut; kunci terendah = perintah terlama.
    const char* oldest = nullptr;
    JsonObject target;
    for (JsonPair kv : cmds) {
        if (oldest == nullptr || strcmp(kv.key().c_str(), oldest) < 0) {
            oldest = kv.key().c_str();
            target = kv.value().as<JsonObject>();
        }
    }

    const char* action = target["action"] | "";
    JsonVariant value = target["value"];
    bool on = value | false;
    bool isRestart = strcmp(action, "restart") == 0;

    if (strcmp(action, "auto") == 0) {
        controlRelease();
    } else if (strcmp(action, "mist") == 0) {
        controlSet(C_MIST, on);
    } else if (strcmp(action, "irigasi") == 0) {
        controlSet(C_IRIG, on);
    } else if (strcmp(action, "blower") == 0) {
        controlSet(C_BLOWER, on);
    } else if (strcmp(action, "set_vpd") == 0) {
        // value = VPD target (kPa). Contoh: {"action":"set_vpd","value":1.2}
        // Hanya rentang hortikultura yang diterima; target 10 kPa membuat
        // sistem tidak pernah misting sama sekali.
        if (value.is<int>() || value.is<float>()) {
            float v = value.as<float>();
            if (phaseSetVpd(v)) {
                Serial.printf("[fb] set VPD target: %.2f kPa\n", v);
            } else {
                Serial.printf("[fb] set_vpd ditolak: %.2f kPa di luar rentang %.1f-%.1f kPa\n",
                              v, VPD_TARGET_MIN, VPD_TARGET_MAX);
            }
        } else {
            Serial.println("[fb] set_vpd: nilai tidak valid");
        }
    } else if (strcmp(action, "set_fase") == 0 || strcmp(action, "set_phase") == 0) {
        // value = nama fase. Contoh: {"action":"set_fase","value":"Generatif"}
        // value kosong/null = matikan mode setpoint/fase (kembali ke fuzzy).
        const char* nm = value | "";
        if (value.isNull() || nm[0] == '\0') {
            phaseDeactivate();
            Serial.println("[fb] fase nonaktif - keputusan sepenuhnya kembali ke fuzzy");
        } else {
            phaseSetName(nm);
            Serial.printf("[fb] set fase: %s\n", nm);
        }
    } else if (strcmp(action, "set_durasi") == 0 || strcmp(action, "set_duration") == 0) {
        // value = angka; unit opsional "detik"|"menit"|"jam"|"hari" (default detik).
        // Contoh: {"action":"set_durasi","value":14,"unit":"hari"}
        float num = value | 0.0f;
        const char* unit = target["unit"] | "detik";
        double sec = num;
        if (strcmp(unit, "menit") == 0)      sec = num * 60.0;
        else if (strcmp(unit, "jam") == 0)   sec = num * 3600.0;
        else if (strcmp(unit, "hari") == 0)  sec = num * 86400.0;
        // Cek overflow uint32 + nilai tidak masuk akal sebelum dikonversi.
        if (isnan(num) || num < 0.0f || sec > (double)PHASE_MAX_DURATION_SEC) {
            Serial.printf("[fb] set_durasi ditolak: %.2f %s (di luar 0..%lu detik)\n",
                          num, unit, (unsigned long)PHASE_MAX_DURATION_SEC);
        } else {
            phaseSetDuration((uint32_t)sec);
            Serial.printf("[fb] set durasi fase: %.0f %s (%lu detik)\n",
                          num, unit, (unsigned long)sec);
        }
    } else if (strcmp(action, "fase_reset") == 0 || strcmp(action, "phase_reset") == 0) {
        phaseReset();
        Serial.println("[fb] fase direset (penghitung durasi mulai dari nol)");
    } else if (!isRestart) {
        Serial.printf("[fb] perintah tidak dikenal: %s\n", action);
    }
    // Log ringkas hanya untuk perintah boolean (set_* sudah mencetak sendiri).
    if (strcmp(action, "auto") == 0 || strcmp(action, "mist") == 0 ||
        strcmp(action, "irigasi") == 0 || strcmp(action, "blower") == 0) {
        Serial.printf("[fb] perintah diterima: %s %d\n", action, on);
    }

    // Tandai selesai dengan menghapus entri. WAJIB sebelum restart — kalau tidak,
    // entri masih ada saat boot dan device restart lagi tanpa henti.
    client.setInsecure();
    String delUrl = String(FIREBASE_DB_URL) + "/commands/" + DEVICE_ID + "/" + oldest
                    + ".json?auth=" + idToken;
    http.begin(client, delUrl);
    int delCode = http.sendRequest("DELETE", "");
    http.end();

    if (isRestart) {
        // ponytail: restart hanya kalau DELETE berhasil. Kalau gagal, jangan restart
        // (perintah akan terbaca lagi). device tetap jalan & bisa di-restart manual.
        if (delCode < 200 || delCode >= 300) {
            Serial.printf("[fb] restart dibatalkan: DELETE perintah gagal (HTTP %d)\n", delCode);
        } else if (restartBlocked()) {
            // Cooldown: reboot berulang memutus siklus irigasi.
            Serial.printf("[fb] restart ditolak: cooldown %lu s\n",
                          (unsigned long)(RESTART_COOLDOWN_MS / 1000UL));
        } else {
            markRestart();
            Serial.println("[fb] perintah restart, perangkat restart...");
            delay(200);   // beri waktu DELETE selesai di sisi server
            ESP.restart();
        }
    }
}
